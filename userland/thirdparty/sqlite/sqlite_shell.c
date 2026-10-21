#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static sqlite3* g_db = 0;
static int g_headers = 1;

static void close_db(void) {
    if (g_db) {
        sqlite3_close(g_db);
        g_db = 0;
    }
}

static int open_db(const char* path) {
    close_db();
    int rc = sqlite3_open(path, &g_db);
    if (rc != SQLITE_OK) {
        printf("error opening %s: %s\n", path, sqlite3_errmsg(g_db));
        close_db();
        return -1;
    }
    return 0;
}

static void print_column_value(sqlite3_stmt* stmt, int i) {
    int type = sqlite3_column_type(stmt, i);
    if (type == SQLITE_NULL) {
        printf("NULL");
    } else if (type == SQLITE_BLOB) {
        int n = sqlite3_column_bytes(stmt, i);
        printf("<blob %d bytes>", n);
    } else {
        const unsigned char* txt = sqlite3_column_text(stmt, i);
        printf("%s", txt ? (const char*)txt : "");
    }
}

static void run_sql(const char* sql) {
    if (!g_db) {
        printf("no database open, use .open <file>\n");
        return;
    }
    const char* tail = sql;
    while (tail && *tail) {
        sqlite3_stmt* stmt = 0;
        const char* next = 0;
        int rc = sqlite3_prepare_v2(g_db, tail, -1, &stmt, &next);
        if (rc != SQLITE_OK) {
            printf("error: %s\n", sqlite3_errmsg(g_db));
            return;
        }
        tail = next;
        if (!stmt) continue;

        int ncol = sqlite3_column_count(stmt);
        int printed_header = 0;
        for (;;) {
            rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                if (g_headers && !printed_header && ncol > 0) {
                    int i;
                    for (i = 0; i < ncol; i++) {
                        if (i) printf("|");
                        printf("%s", sqlite3_column_name(stmt, i));
                    }
                    printf("\n");
                    printed_header = 1;
                }
                int i;
                for (i = 0; i < ncol; i++) {
                    if (i) printf("|");
                    print_column_value(stmt, i);
                }
                printf("\n");
            } else {
                break;
            }
        }
        if (rc != SQLITE_DONE) {
            printf("error: %s\n", sqlite3_errmsg(g_db));
        }
        sqlite3_finalize(stmt);
    }
}

static void cmd_tables(void) {
    run_sql("SELECT name FROM sqlite_schema WHERE type='table' ORDER BY name;");
}

static void cmd_schema(const char* arg) {
    char buf[512];
    if (arg && arg[0]) {
        snprintf(buf, sizeof(buf),
                 "SELECT sql FROM sqlite_schema WHERE name='%s';", arg);
    } else {
        snprintf(buf, sizeof(buf),
                 "SELECT sql FROM sqlite_schema ORDER BY name;");
    }
    run_sql(buf);
}

static void trim(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                      s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = 0;
    }
}

static int handle_dot(char* line) {
    char* cmd = line + 1;
    char* arg = strchr(cmd, ' ');
    if (arg) { *arg = 0; arg++; while (*arg == ' ') arg++; }

    if (strcmp(cmd, "open") == 0 && arg && arg[0]) {
        open_db(arg);
    } else if (strcmp(cmd, "close") == 0) {
        close_db();
    } else if (strcmp(cmd, "tables") == 0) {
        cmd_tables();
    } else if (strcmp(cmd, "schema") == 0) {
        cmd_schema(arg);
    } else if (strcmp(cmd, "headers") == 0) {
        if (arg && strcmp(arg, "off") == 0) g_headers = 0;
        else g_headers = 1;
    } else if (strcmp(cmd, "help") == 0) {
        printf(".open <file>      open or create a database file\n");
        printf(".close            close the current database\n");
        printf(".tables           list tables\n");
        printf(".schema [table]   show schema\n");
        printf(".headers on|off   toggle column headers\n");
        printf(".exit / .quit     leave the sqlite shell\n");
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        return 1;
    } else {
        printf("unknown command: .%s\n", cmd);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (sqlite3_initialize() != SQLITE_OK) {
        printf("sqlite3_initialize failed\n");
        return 1;
    }

    if (argc > 1) {
        if (open_db(argv[1]) != 0) {
            return 1;
        }
    }

    printf("CrusadIA sqlite shell, sqlite3 %s\n", sqlite3_libversion());
    printf("type SQL statements terminated by ';' or '.help' for commands\n");

    char line[1024];
    char stmtbuf[4096];
    stmtbuf[0] = 0;

    for (;;) {
        printf(g_db ? "sqlite> " : "sqlite (no db)> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        trim(line);

        if (stmtbuf[0] == 0 && line[0] == '.') {
            if (handle_dot(line)) break;
            continue;
        }

        if (strlen(stmtbuf) + strlen(line) + 2 < sizeof(stmtbuf)) {
            strcat(stmtbuf, line);
            strcat(stmtbuf, "\n");
        }

        if (strchr(line, ';')) {
            run_sql(stmtbuf);
            stmtbuf[0] = 0;
        }
    }

    close_db();
    sqlite3_shutdown();
    return 0;
}
