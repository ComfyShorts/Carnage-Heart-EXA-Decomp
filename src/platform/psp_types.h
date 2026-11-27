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
#define SCE_ERROR_ERRNO_ENAMETOOLONG ((int32_t)0x8001005B)
#define SCE_KERNEL_ERROR_WAIT_TIMEOUT ((int32_t)0x800201A8)
#define SCE_KERNEL_ERROR_BADF   ((int32_t)0x80020323)
#define SCE_KERNEL_ERROR_UNSUP  ((int32_t)0x80020325)
#define SCE_KERNEL_ERROR_ASYNC_BUSY ((int32_t)0x80020329)
#define SCE_KERNEL_ERROR_NOASYNC    ((int32_t)0x8002032A)

/* ---- sceUtility savedata (SDK layout) ----
 * Checked against the game's CUtility (FUN_08b25354 and the InitStart* helpers): the
 * param block sits at CUtility + 0x648, mode at +0x30, gameName +0x3C, saveName +0x4C,
 * fileName +0x64, dataBuf +0x74, the SFO strings from +0x80, icon0 +0x584, newData
 * +0x5C4, key +0x5DC, fileList +0x5F8. */

typedef struct pspUtilityDialogCommon {
    uint32_t size;           /* sizeof the whole param block */
    int32_t language;
    int32_t buttonSwap;
    int32_t graphicsThread, accessThread, fontThread, soundThread;
    int32_t result;          /* 0x1C: the operation's result, set when it finishes */
    int32_t reserved[4];
} pspUtilityDialogCommon;

typedef struct PspUtilitySavedataSFOParam {
    char title[0x80];
    char savedataTitle[0x80];
    char detail[0x400];
    uint8_t parentalLevel;
    uint8_t unknown[3];
} PspUtilitySavedataSFOParam;

typedef struct PspUtilitySavedataFileData {
    void *buf;
    SceSize bufSize;
    SceSize size;            /* bytes used */
    int32_t unknown;
} PspUtilitySavedataFileData;

typedef struct PspUtilitySavedataListSaveNewData {
    PspUtilitySavedataFileData icon0;
    char *title;
} PspUtilitySavedataListSaveNewData;

/* SceUtilitySavedataFileListEntry: a short stat and the file name, 0x50 bytes (the game
 * steps its lists by 0x50 and reads the name at +0x40, the size at +8, mtime at +0x30). */
typedef struct SceUtilitySavedataFileStat {
    SceMode st_mode;
    uint32_t st_attr;
    SceOff st_size;
    ScePspDateTime sce_ctime, sce_atime, sce_mtime;
} SceUtilitySavedataFileStat;

typedef struct SceUtilitySavedataFileListEntry {
    SceUtilitySavedataFileStat stat;
    char name[16];
} SceUtilitySavedataFileListEntry;

typedef struct SceUtilitySavedataFileListInfo {
    uint32_t maxSecureEntries, maxNormalEntries, maxSystemEntries;
    uint32_t resultNumSecureEntries, resultNumNormalEntries, resultNumSystemEntries;
    SceUtilitySavedataFileListEntry *secureEntries, *normalEntries, *systemEntries;
} SceUtilitySavedataFileListInfo;

typedef struct SceUtilitySavedataSizeEntry {
    uint64_t size __attribute__((aligned(8)));
    char name[16];
} SceUtilitySavedataSizeEntry;

typedef struct SceUtilitySavedataSizeInfo {
    int32_t numSecureEntries, numNormalEntries;
    SceUtilitySavedataSizeEntry *secureEntries, *normalEntries;
    int32_t sectorSize, freeSectors, freeKB;
    char freeString[8];
    int32_t neededKB;
    char neededString[8];
    int32_t overwriteKB;
    char overwriteString[8];
} SceUtilitySavedataSizeInfo;

typedef struct SceUtilitySavedataIdListEntry {
    int32_t st_mode;
    ScePspDateTime sce_ctime, sce_atime, sce_mtime;
    char name[20];
} SceUtilitySavedataIdListEntry;

typedef struct SceUtilitySavedataIdListInfo {
    int32_t maxCount, resultCount;
    SceUtilitySavedataIdListEntry *entries;
} SceUtilitySavedataIdListInfo;

typedef struct SceUtilitySavedataMsFreeInfo {
    int32_t clusterSize, freeClusters, freeSpaceKB;
    char freeSpaceStr[8];
} SceUtilitySavedataMsFreeInfo;

typedef struct SceUtilitySavedataUsedDataInfo {
    int32_t usedClusters, usedSpaceKB;
    char usedSpaceStr[8];
    int32_t usedSpace32KB;
    char usedSpace32Str[8];
} SceUtilitySavedataUsedDataInfo;

typedef struct SceUtilitySavedataParam {
    pspUtilityDialogCommon base;      /* 0x000 */
    int32_t mode;                     /* 0x030: PSP_UTILITY_SAVEDATA_* */
    int32_t bind;
    int32_t overwrite;
    char gameName[13];                /* 0x03C */
    char reserved[3];
    char saveName[20];                /* 0x04C */
    char (*saveNameList)[20];         /* 0x060: "" terminates */
    char fileName[13];                /* 0x064 */
    char reserved1[3];
    void *dataBuf;                    /* 0x074 */
    SceSize dataBufSize;
    SceSize dataSize;
    PspUtilitySavedataSFOParam sfoParam; /* 0x080 */
    PspUtilitySavedataFileData icon0FileData; /* 0x584 */
    PspUtilitySavedataFileData icon1FileData;
    PspUtilitySavedataFileData pic1FileData;
    PspUtilitySavedataFileData snd0FileData;
    PspUtilitySavedataListSaveNewData *newData; /* 0x5C4 */
    int32_t focus;
    int32_t abortStatus;
    SceUtilitySavedataMsFreeInfo *msFree;     /* 0x5D0 */
    SceUtilitySavedataUsedDataInfo *msData;
    SceUtilitySavedataUsedDataInfo *utilityData;
    uint8_t key[16];                  /* 0x5DC */
    uint32_t secureVersion;
    int32_t multiStatus;
    SceUtilitySavedataIdListInfo *idList;     /* 0x5F4 */
    SceUtilitySavedataFileListInfo *fileList; /* 0x5F8 */
    SceUtilitySavedataSizeInfo *sizeInfo;     /* 0x5FC */
} SceUtilitySavedataParam;

#ifdef PCPORT_M32
_Static_assert(sizeof(SceUtilitySavedataFileListEntry) == 0x50, "FileListEntry layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, mode) == 0x30, "savedata layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, fileName) == 0x64, "savedata layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, dataBuf) == 0x74, "savedata layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, icon0FileData) == 0x584, "savedata layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, newData) == 0x5C4, "savedata layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, key) == 0x5DC, "savedata layout");
_Static_assert(__builtin_offsetof(SceUtilitySavedataParam, fileList) == 0x5F8, "savedata layout");
_Static_assert(sizeof(SceUtilitySavedataParam) == 0x600, "savedata layout");
#endif

enum {
    PSP_UTILITY_SAVEDATA_AUTOLOAD = 0, PSP_UTILITY_SAVEDATA_AUTOSAVE = 1,
    PSP_UTILITY_SAVEDATA_LOAD = 2, PSP_UTILITY_SAVEDATA_SAVE = 3,
    PSP_UTILITY_SAVEDATA_LISTLOAD = 4, PSP_UTILITY_SAVEDATA_LISTSAVE = 5,
    PSP_UTILITY_SAVEDATA_LISTDELETE = 6, PSP_UTILITY_SAVEDATA_LISTALLDELETE = 7,
    PSP_UTILITY_SAVEDATA_SIZES = 8, PSP_UTILITY_SAVEDATA_AUTODELETE = 9,
    PSP_UTILITY_SAVEDATA_DELETE = 10, PSP_UTILITY_SAVEDATA_LIST = 11,
    PSP_UTILITY_SAVEDATA_FILES = 12, PSP_UTILITY_SAVEDATA_MAKEDATASECURE = 13,
    PSP_UTILITY_SAVEDATA_MAKEDATA = 14, PSP_UTILITY_SAVEDATA_READSECURE = 15,
    PSP_UTILITY_SAVEDATA_READ = 16, PSP_UTILITY_SAVEDATA_WRITESECURE = 17,
    PSP_UTILITY_SAVEDATA_WRITE = 18, PSP_UTILITY_SAVEDATA_ERASESECURE = 19,
    PSP_UTILITY_SAVEDATA_ERASE = 20, PSP_UTILITY_SAVEDATA_DELETEDATA = 21,
    PSP_UTILITY_SAVEDATA_GETSIZE = 22,
};

/* sceUtility*GetStatus */
enum { PSP_UTILITY_STATUS_NONE, PSP_UTILITY_STATUS_INIT, PSP_UTILITY_STATUS_VISIBLE,
       PSP_UTILITY_STATUS_QUIT, PSP_UTILITY_STATUS_FINISHED };

#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_MS      ((int32_t)0x80110301)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN ((int32_t)0x80110306)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA    ((int32_t)0x80110307)
#define SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM      ((int32_t)0x80110308)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_MS      ((int32_t)0x80110381)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_NO_SPACE   ((int32_t)0x80110383)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_MS_PROTECTED ((int32_t)0x80110384)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_ACCESS_ERROR ((int32_t)0x80110385)
#define SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM      ((int32_t)0x80110388)
#define SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA  ((int32_t)0x80110347)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_NO_MEMSTICK  ((int32_t)0x80110321)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_ACCESS_ERROR ((int32_t)0x80110325)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_DATA_BROKEN  ((int32_t)0x80110326)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA      ((int32_t)0x80110327)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS   ((int32_t)0x80110328)
#define SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND ((int32_t)0x80110329)
#define SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA   ((int32_t)0x801103C7)
#define SCE_ERROR_UTILITY_INVALID_STATUS           ((int32_t)0x80110001)

/* sceUmdGetDriveStat bits */
#define PSP_UMD_NOT_PRESENT 0x01
#define PSP_UMD_PRESENT     0x02
#define PSP_UMD_CHANGED     0x04
#define PSP_UMD_INITING     0x08
#define PSP_UMD_INITED      0x10
#define PSP_UMD_READY       0x20

#endif
