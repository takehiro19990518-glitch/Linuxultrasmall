#include <errno.h>

const char *strerror(int err)
{
    switch (err) {
    case E_OK:          return "success";
    case E_IO:          return "I/O error";
    case E_TIMEOUT:     return "device timeout";
    case E_RANGE:       return "sector out of range";
    case E_NODEV:       return "no file system mounted";
    case E_NOMEM:       return "out of memory";
    case E_NOENT:       return "no such file or directory";
    case E_EXIST:       return "already exists";
    case E_NOTDIR:      return "not a directory";
    case E_ISDIR:       return "is a directory";
    case E_NOSPC:       return "no space left on device";
    case E_NOTEMPTY:    return "directory not empty";
    case E_INVAL:       return "invalid argument";
    case E_NAMETOOLONG: return "name too long (max 27 characters)";
    case E_FBIG:        return "file too large";
    case E_CORRUPT:     return "file system structure is corrupt";
    case E_BUSY:        return "busy";
    default:            return "unknown error";
    }
}
