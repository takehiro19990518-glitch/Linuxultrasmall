/* Kernel error codes (negative return values). */
#ifndef P5_ERRNO_H
#define P5_ERRNO_H
enum {
    E_OK = 0,
    E_IO = -1,          /* device reported an error */
    E_TIMEOUT = -2,     /* device did not respond in time */
    E_RANGE = -3,       /* block/sector outside the device or partition */
    E_NODEV = -4,       /* no such device / nothing mounted */
    E_NOMEM = -5,
    E_NOENT = -6,       /* no such file or directory */
    E_EXIST = -7,
    E_NOTDIR = -8,
    E_ISDIR = -9,
    E_NOSPC = -10,      /* no free blocks or inodes */
    E_NOTEMPTY = -11,
    E_INVAL = -12,
    E_NAMETOOLONG = -13,
    E_FBIG = -14,       /* file too large */
    E_CORRUPT = -15,    /* on-disk structure failed validation */
    E_BUSY = -16,
};
const char *strerror(int err);
#endif
