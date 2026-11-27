/* PSP SDK types and constants used by the shim, laid out as on the PSP.
 *
 * The PSP's MIPS EABI aligns 64-bit integers to 8 bytes; i386 only aligns them to 4
 * inside structs. SceOff/SceInt64 carry an explicit 8-byte alignment so structs that hold
 * them keep the PSP layout under -m32. */
#ifndef PSP_TYPES_H
#define PSP_TYPES_H

#include <stdint.h>

typedef int32_t SceUID;
typedef uint32_t SceSize;
typedef int32_t SceMode;
typedef int64_t SceOff __attribute__((aligned(8)));
typedef int64_t SceInt64 __attribute__((aligned(8)));

typedef struct ScePspDateTime {
    uint16_t year, month, day, hour, minute, second;
    uint32_t microsecond;
} ScePspDateTime;

typedef struct SceIoStat {
    SceMode st_mode;
    uint32_t st_attr;
    SceOff st_size;
    /* PSPSDK calls these st_ctime/st_atime/st_mtime, which are macros in glibc. */
    ScePspDateTime sce_ctime;
    ScePspDateTime sce_atime;
    ScePspDateTime sce_mtime;
    uint32_t st_private[6];
} SceIoStat;

typedef struct SceIoDirent {
    SceIoStat d_stat;
    char d_name[256];
    void *d_private;
    int32_t dummy;
} SceIoDirent;

#ifdef PCPORT_M32
_Static_assert(sizeof(ScePspDateTime) == 16, "ScePspDateTime layout");
_Static_assert(sizeof(SceIoStat) == 0x58, "SceIoStat layout");
_Static_assert(__builtin_offsetof(SceIoStat, st_size) == 8, "SceIoStat layout");
_Static_assert(sizeof(SceIoDirent) == 0x160, "SceIoDirent layout");
#endif

/* sceIoOpen flags */
#define PSP_O_RDONLY 0x0001
#define PSP_O_WRONLY 0x0002
#define PSP_O_RDWR   0x0003
#define PSP_O_APPEND 0x0100
#define PSP_O_CREAT  0x0200
#define PSP_O_TRUNC  0x0400
#define PSP_O_EXCL   0x0800

/* sceIoLseek whence */
#define PSP_SEEK_SET 0
#define PSP_SEEK_CUR 1
#define PSP_SEEK_END 2

/* SceIoStat.st_mode / st_attr */
#define FIO_S_IFDIR  0x1000
#define FIO_S_IFREG  0x2000
#define FIO_SO_IFDIR 0x0010
#define FIO_SO_IFREG 0x0020

/* Errors. File-system errnos are 0x80010000 | errno (newlib numbering). */
#define SCE_ERROR_ERRNO_ENOENT  ((int32_t)0x80010002)
#define SCE_ERROR_ERRNO_EIO     ((int32_t)0x80010005)
#define SCE_ERROR_ERRNO_EACCES  ((int32_t)0x8001000D)
#define SCE_ERROR_ERRNO_EEXIST  ((int32_t)0x80010011)
#define SCE_ERROR_ERRNO_ENODEV  ((int32_t)0x80010013)
#define SCE_ERROR_ERRNO_ENOTDIR ((int32_t)0x80010014)
#define SCE_ERROR_ERRNO_EISDIR  ((int32_t)0x80010015)
#define SCE_ERROR_ERRNO_EINVAL  ((int32_t)0x80010016)
#define SCE_ERROR_ERRNO_EMFILE  ((int32_t)0x80010018)
#define SCE_ERROR_ERRNO_EROFS   ((int32_t)0x8001001E)
#define SCE_KERNEL_ERROR_WAIT_TIMEOUT ((int32_t)0x800201A8)
#define SCE_KERNEL_ERROR_BADF   ((int32_t)0x80020323)
#define SCE_KERNEL_ERROR_UNSUP  ((int32_t)0x80020325)
#define SCE_KERNEL_ERROR_ASYNC_BUSY ((int32_t)0x80020329)
#define SCE_KERNEL_ERROR_NOASYNC    ((int32_t)0x8002032A)

/* sceUmdGetDriveStat bits */
#define PSP_UMD_NOT_PRESENT 0x01
#define PSP_UMD_PRESENT     0x02
#define PSP_UMD_CHANGED     0x04
#define PSP_UMD_INITING     0x08
#define PSP_UMD_INITED      0x10
#define PSP_UMD_READY       0x20

#endif
