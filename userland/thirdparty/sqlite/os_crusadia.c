#include "sqlite3.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct CrusadiaFile {
    sqlite3_file base;
    int fd;
    int deleteOnClose;
    char path[512];
} CrusadiaFile;

static unsigned int g_rng_state = 0;

static unsigned int crusadia_rand_next(void) {
    if (g_rng_state == 0) {
        g_rng_state = (unsigned int)time(0) ^ 0x9e3779b9u;
        if (g_rng_state == 0) g_rng_state = 0xdeadbeefu;
    }
    g_rng_state ^= g_rng_state << 13;
    g_rng_state ^= g_rng_state >> 17;
    g_rng_state ^= g_rng_state << 5;
    return g_rng_state;
}

static int crusadiaClose(sqlite3_file* pFile) {
    CrusadiaFile* p = (CrusadiaFile*)pFile;
    if (p->fd >= 0) close(p->fd);
    if (p->deleteOnClose) unlink(p->path);
    return SQLITE_OK;
}

static int crusadiaRead(sqlite3_file* pFile, void* buf, int iAmt, sqlite3_int64 iOfst) {
    CrusadiaFile* p = (CrusadiaFile*)pFile;
    if (lseek(p->fd, (long)iOfst, SEEK_SET) < 0) return SQLITE_IOERR_READ;
    long got = read(p->fd, buf, (size_t)iAmt);
    if (got < 0) return SQLITE_IOERR_READ;
    if (got < iAmt) {
        memset((char*)buf + got, 0, (size_t)(iAmt - got));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int crusadiaWrite(sqlite3_file* pFile, const void* buf, int iAmt, sqlite3_int64 iOfst) {
    CrusadiaFile* p = (CrusadiaFile*)pFile;
    if (lseek(p->fd, (long)iOfst, SEEK_SET) < 0) return SQLITE_IOERR_WRITE;
    long wrote = write(p->fd, buf, (size_t)iAmt);
    if (wrote != iAmt) return SQLITE_IOERR_WRITE;
    return SQLITE_OK;
}

static int crusadiaTruncate(sqlite3_file* pFile, sqlite3_int64 size) {
    CrusadiaFile* p = (CrusadiaFile*)pFile;
    (void)p; (void)size;
    return SQLITE_OK;
}

static int crusadiaSync(sqlite3_file* pFile, int flags) {
    (void)pFile; (void)flags;
    return SQLITE_OK;
}

static int crusadiaFileSize(sqlite3_file* pFile, sqlite3_int64* pSize) {
    CrusadiaFile* p = (CrusadiaFile*)pFile;
    long cur = lseek(p->fd, 0, SEEK_CUR);
    long end = lseek(p->fd, 0, SEEK_END);
    if (end < 0) return SQLITE_IOERR_FSTAT;
    if (cur >= 0) lseek(p->fd, cur, SEEK_SET);
    *pSize = (sqlite3_int64)end;
    return SQLITE_OK;
}

static int crusadiaLock(sqlite3_file* pFile, int eLock) {
    (void)pFile; (void)eLock;
    return SQLITE_OK;
}

static int crusadiaUnlock(sqlite3_file* pFile, int eLock) {
    (void)pFile; (void)eLock;
    return SQLITE_OK;
}

static int crusadiaCheckReservedLock(sqlite3_file* pFile, int* pResOut) {
    (void)pFile;
    *pResOut = 0;
    return SQLITE_OK;
}

static int crusadiaFileControl(sqlite3_file* pFile, int op, void* pArg) {
    (void)pFile; (void)op; (void)pArg;
    return SQLITE_NOTFOUND;
}

static int crusadiaSectorSize(sqlite3_file* pFile) {
    (void)pFile;
    return 512;
}

static int crusadiaDeviceCharacteristics(sqlite3_file* pFile) {
    (void)pFile;
    return 0;
}

static const sqlite3_io_methods crusadia_io_methods = {
    1,
    crusadiaClose,
    crusadiaRead,
    crusadiaWrite,
    crusadiaTruncate,
    crusadiaSync,
    crusadiaFileSize,
    crusadiaLock,
    crusadiaUnlock,
    crusadiaCheckReservedLock,
    crusadiaFileControl,
    crusadiaSectorSize,
    crusadiaDeviceCharacteristics,
    0, 0, 0, 0, 0, 0
};

static void crusadia_temp_name(char* out, size_t outsz) {
    unsigned int r = crusadia_rand_next();
    const char* digits = "0123456789abcdef";
    char tmp[16];
    int i;
    for (i = 0; i < 8; i++) {
        tmp[i] = digits[(r >> (i * 4)) & 0xF];
    }
    tmp[8] = 0;
    out[0] = '/';
    out[1] = 's';
    out[2] = 'q';
    out[3] = '_';
    out[4] = 0;
    strncat(out, tmp, outsz - strlen(out) - 1);
    strncat(out, ".tmp", outsz - strlen(out) - 1);
}

static int crusadiaOpen(sqlite3_vfs* pVfs, const char* zName, sqlite3_file* pFile,
                         int flags, int* pOutFlags) {
    (void)pVfs;
    CrusadiaFile* p = (CrusadiaFile*)pFile;
    memset(p, 0, sizeof(*p));
    p->base.pMethods = &crusadia_io_methods;
    p->fd = -1;

    char namebuf[512];
    if (zName == 0) {
        crusadia_temp_name(namebuf, sizeof(namebuf));
        zName = namebuf;
        flags |= SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
    }
    strncpy(p->path, zName, sizeof(p->path) - 1);

    int oflags = 0;
    if (flags & SQLITE_OPEN_EXCLUSIVE) oflags |= O_EXCL;
    if (flags & SQLITE_OPEN_CREATE) oflags |= O_CREAT;
    if (flags & SQLITE_OPEN_READWRITE) oflags |= O_RDWR;
    else oflags |= O_RDONLY;

    int fd = open(zName, oflags);
    if (fd < 0 && (flags & SQLITE_OPEN_READWRITE) && !(flags & SQLITE_OPEN_CREATE)) {
        return SQLITE_CANTOPEN;
    }
    if (fd < 0) return SQLITE_CANTOPEN;

    p->fd = fd;
    p->deleteOnClose = (flags & SQLITE_OPEN_DELETEONCLOSE) != 0;
    if (pOutFlags) *pOutFlags = flags;
    return SQLITE_OK;
}

static int crusadiaDelete(sqlite3_vfs* pVfs, const char* zName, int syncDir) {
    (void)pVfs; (void)syncDir;
    unlink(zName);
    return SQLITE_OK;
}

static int crusadiaAccess(sqlite3_vfs* pVfs, const char* zName, int flags, int* pResOut) {
    (void)pVfs;
    int oflags = (flags == SQLITE_ACCESS_READWRITE) ? O_RDWR : O_RDONLY;
    int fd = open(zName, oflags);
    if (fd < 0) {
        *pResOut = 0;
    } else {
        close(fd);
        *pResOut = 1;
    }
    return SQLITE_OK;
}

static int crusadiaFullPathname(sqlite3_vfs* pVfs, const char* zName, int nOut, char* zOut) {
    (void)pVfs;
    strncpy(zOut, zName, (size_t)nOut - 1);
    zOut[nOut - 1] = 0;
    return SQLITE_OK;
}

static void* crusadiaDlOpen(sqlite3_vfs* pVfs, const char* zFilename) {
    (void)pVfs; (void)zFilename;
    return 0;
}

static void crusadiaDlError(sqlite3_vfs* pVfs, int nByte, char* zErrMsg) {
    (void)pVfs;
    if (nByte > 0) zErrMsg[0] = 0;
}

static void (*crusadiaDlSym(sqlite3_vfs* pVfs, void* pH, const char* z))(void) {
    (void)pVfs; (void)pH; (void)z;
    return 0;
}

static void crusadiaDlClose(sqlite3_vfs* pVfs, void* pHandle) {
    (void)pVfs; (void)pHandle;
}

static int crusadiaRandomness(sqlite3_vfs* pVfs, int nByte, char* zOut) {
    (void)pVfs;
    int i;
    for (i = 0; i < nByte; i++) {
        zOut[i] = (char)(crusadia_rand_next() & 0xFF);
    }
    return nByte;
}

static int crusadiaSleep(sqlite3_vfs* pVfs, int microseconds) {
    (void)pVfs;
    int loops = microseconds / 1000;
    int i;
    for (i = 0; i < loops; i++) {
        yield();
    }
    return microseconds;
}

static int crusadiaCurrentTimeInt64(sqlite3_vfs* pVfs, sqlite3_int64* pNow) {
    (void)pVfs;
    static const sqlite3_int64 unixEpoch = 24405875LL * 8640000LL;
    sqlite3_int64 t = (sqlite3_int64)time(0);
    *pNow = t * 1000 + unixEpoch;
    return SQLITE_OK;
}

static int crusadiaCurrentTime(sqlite3_vfs* pVfs, double* pNow) {
    sqlite3_int64 i = 0;
    crusadiaCurrentTimeInt64(pVfs, &i);
    *pNow = i / 86400000.0;
    return SQLITE_OK;
}

static int crusadiaGetLastError(sqlite3_vfs* pVfs, int nBuf, char* zBuf) {
    (void)pVfs;
    if (nBuf > 0) zBuf[0] = 0;
    return 0;
}

static sqlite3_vfs crusadia_vfs = {
    2,
    sizeof(CrusadiaFile),
    512,
    0,
    "crusadia",
    0,
    crusadiaOpen,
    crusadiaDelete,
    crusadiaAccess,
    crusadiaFullPathname,
    crusadiaDlOpen,
    crusadiaDlError,
    crusadiaDlSym,
    crusadiaDlClose,
    crusadiaRandomness,
    crusadiaSleep,
    crusadiaCurrentTime,
    crusadiaGetLastError,
    crusadiaCurrentTimeInt64
};

int sqlite3_os_init(void) {
    return sqlite3_vfs_register(&crusadia_vfs, 1);
}

int sqlite3_os_end(void) {
    return SQLITE_OK;
}
