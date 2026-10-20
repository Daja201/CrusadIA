#include "library.h"
#include "fs.h"
#include "klog.h"
#include "string.h"

typedef struct {
    const char* name;
    const char* section;
    const char* usage;
    const char* desc;
} lib_entry_t;

static const lib_entry_t lib_entries[] = {
    // BASIC
    {"help",     "BASIC", "help",                    "some basic info"},
    {"clear",    "BASIC", "clear",                   "clears display"},
    {"cow",      "BASIC", "cow",                      "makes an cow jump through your window"},
    {"meow",     "BASIC", "meow",                     "prints cute cat"},
    {"reboot",   "BASIC", "reboot",                   "reboots using triple fault"},
    {"shutdown", "BASIC", "shutdown",                 "shuts down the system"},
    {"mem",      "BASIC", "mem",                      "shows free memory"},
    {"lib",      "BASIC", "lib [command]",            "bro u are using lib rn you should know what it does"},
    {"ld",       "FS", "ld",                          "lists drives, their filesystem and mount points"},
    {"mount",    "FS", "mount <drive> <location>",    "mounts a drive (fs autodetected), e.g. mount d1.0 /mount/usb"},
    {"umount",   "FS", "umount <drive|location>",     "unmounts a drive or mount point"},
    {"setroot",  "FS", "setroot <drive>",             "sets the root drive mounted at /"},
    {"use",      "FS", "use <drive>",                 "mounts the drive under /mount if needed and cd's into it"},
    {"fs",       "FS", "fs",                          "shows all mounted filesystems"},
    {"read",     "FS", "read <file>",                 "prints a file"},
    {"ls",       "FS", "ls [-l] [-R] [-1] [-a] [path]...", "lists one or more directories"},
    {"cd",       "FS", "cd <dir>",                    "changes current directory"},
    {"pwd",      "FS", "pwd",                         "prints current directory"},
    {"mkdir",    "FS", "mkdir [-p] <dir>...",         "makes one or more directories"},
    {"rm",       "FS", "rm [-r] [-f] <path>...",      "deletes files or, with -r, directories"},
    {"cp",       "FS", "cp [-r] <src>... <dst>",      "copies files, or whole directories with -r"},
    {"mv",       "FS", "mv <src>... <dst>",           "moves/renames files or directories"},
    {"touch",    "FS", "touch <file>...",             "creates one or more empty files"},
    {"cat",      "FS", "cat [-n] <file>...",          "prints file contents, -n numbers lines"},
    {"wr",       "FS", "wr <file> <content>",         "writes into file"},
    {"fd",       "FS", "fd <tag>",                    "finds files by tag (cosfs only)"},
    {"format",   "FS", "format <drive> <fat32|cos>",  "formats an unmounted or mounted drive, remounts it afterwards"},
    {"head",     "TEXT", "head [-n N] <file>",        "prints the first N lines of a file (default 10)"},
    {"tail",     "TEXT", "tail [-n N] <file>",        "prints the last N lines of a file (default 10)"},
    {"wc",       "TEXT", "wc [-lwc] <file>",          "counts lines, words and bytes in a file"},
    {"grep",     "TEXT", "grep [-nv] <pattern> <file>", "prints lines matching (or not matching) a literal substring"},
    {"sort",     "TEXT", "sort [-r] <file>",          "prints a file's lines sorted alphabetically"},
    {"uniq",     "TEXT", "uniq <file>",               "collapses adjacent duplicate lines"},
    {"echo",     "TEXT", "echo [-n] [-e] <text>...",  "prints text; -n suppresses newline, -e enables \\n/\\t escapes"},
    {"printf",   "TEXT", "printf <format> [args...]", "formats and prints args, supports %s %d %% and \\n \\t"},
    {"true",     "UTIL", "true",                      "does nothing, succeeds"},
    {"false",    "UTIL", "false",                     "does nothing, used as a no-op placeholder"},
    {"whoami",   "UTIL", "whoami",                    "prints the current user (always 'root', no user system exists)"},
    {"uname",    "UTIL", "uname",                     "prints the operating system name"},
    {"basename", "UTIL", "basename <path>",            "prints the final component of a path"},
    {"dirname",  "UTIL", "dirname <path>",             "prints a path with its final component removed"},
    {"which",    "UTIL", "which <command>",            "shows whether a command exists"},
    {"date",     "UTIL", "date",                       "alias for time, shows date/time from RealTimeClock"},
    // ADVANCED
    {"time",     "ADVANCED", "time",                  "shows time from RealTimeClock"},
    {"sett",     "ADVANCED", "sett <0-12>",                  "sets timezone"},
    {"usb",     "ADVANCED", "usb",                  "shows available usb devices"},
    {"ss",     "ADVANCED", "ss <character>",                  "sends serial on COM1"},
    {"app",      "ADVANCED", "app <app_name>",         "runs an app"},
    {"cc",       "ADVANCED", "cc <file.c> [args] | cc -e \"<code>\"", "compiles a C file in memory with TinyCC and runs it"},
    {"guidemo",  "ADVANCED", "guidemo [crash]",         "ring3 API self-test: klogf()'s before/after each step so a freeze/crash shows exactly where; 'crash' also tests the forbidden-op/fault path"},
};

static const int lib_entry_count = sizeof(lib_entries) / sizeof(lib_entry_t);

static void library_print_all(void) {
    vesa_clear(0X000000);
    //
    kklog("This is an simple operating system made by David Zapletal. Github: Daja201");
    kklog("Here is simple library for using this operating system.");
    kklog("Run 'lib <command>' for detailed info about a specific command.");
    //
    const char* current_section = "";
    for (int i = 0; i < lib_entry_count; i++) {
        if (strcmp(current_section, lib_entries[i].section) != 0) {
            current_section = lib_entries[i].section;
            klogf("  %s:\n", current_section);
        }
        klogf("%s - %s\n", lib_entries[i].name, lib_entries[i].desc);
    }
}

static void library_print_entry(const char* name) {
    for (int i = 0; i < lib_entry_count; i++) {
        if (strcmp(lib_entries[i].name, name) == 0) {
            klogf("NAME:\n    %s\n", lib_entries[i].name);
            klogf("SECTION:\n    %s\n", lib_entries[i].section);
            klogf("USAGE:\n    %s\n", lib_entries[i].usage);
            klogf("DESCRIPTION:\n    %s\n", lib_entries[i].desc);
            return;
        }
    }
    klogf("No manual entry for '%s'\n", name);
    kklog("Run 'lib' with no arguments to see all commands.");
}

void library(int argc, char** argv) {
    if (argc < 2) {
        library_print_all();
        return;
    }
    library_print_entry(argv[1]);
}