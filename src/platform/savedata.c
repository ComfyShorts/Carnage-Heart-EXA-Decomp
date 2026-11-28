/* PSP shim: sceUtilitySavedata*, the system's save-data utility.
 *
 * Saves live in a host folder (psp_savedata_set_root, the --saves flag), laid out as
 * the memory stick's PSP/SAVEDATA folder: one directory per save, named gameName +
 * saveName, holding the game's files and a PARAM.SFO in the system's format (TITLE,
 * SAVEDATA_TITLE, SAVEDATA_DETAIL, SAVEDATA_DIRECTORY, SAVEDATA_FILE_LIST, ...), plus
 * ICON0.PNG / PIC1.PNG when the game hands them over.
 *
 * Differences from the PSP:
 *   - Files are stored as given. The PSP encrypts the "secure" files (the ones written
 *     with a secure mode, listed in SAVEDATA_FILE_LIST) with the game's key; here the
 *     list is kept so FILES can tell secure from normal files, but nothing is encrypted
 *     or hashed.
 *   - The dialog modes (AUTOLOAD/LOAD/LISTLOAD/..., which show the system's save list)
 *     draw nothing: the list modes pick the first name that fits, and the result is the
 *     one the dialog would give after the player confirmed.
 *   - Every operation is done in InitStart. The status still steps INIT -> VISIBLE ->
 *     QUIT -> FINISHED -> NONE through GetStatus/Update/ShutdownStart, as games poll it.
 *   - Free space is reported as 1 GB. */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "host_compat.h"
#include "psp_imports.h"
#include "psp_shim.h"

#define FREE_KB (1024 * 1024)
#define SFO_FILE_LIST_SIZE 0xC60 /* 99 entries of 0x20: name[13], hash[16], pad[3] */
#define SFO_PARAMS_SIZE 0x80

static char g_root[1024] = "saves";
static int g_status;
static SceUtilitySavedataParam *g_param;

void psp_savedata_set_root(const char *dir)
{
    snprintf(g_root, sizeof g_root, "%s", dir && *dir ? dir : "saves");
    size_t n = strlen(g_root);
    while (n > 1 && host_is_dir_sep(g_root[n - 1]) && !(n == 3 && g_root[1] == ':'))
        g_root[--n] = 0;
}

const char *psp_savedata_root(void)
{
    return g_root;
}

/* ---- names and paths ---- */

static bool name_ok(const char *s)
{
    if (!*s || !strcmp(s, ".") || !strcmp(s, ".."))
        return false;
    for (; *s; s++)
        if (*s == '/' || *s == '\\' || *s == ':')
            return false;
    return true;
}

/* A fixed-size char field as a C string (the SDK fields aren't always terminated). */
static void field(char *out, size_t cap, const char *src, size_t len)
{
    size_t n = strnlen(src, len);
    if (n >= cap)
        n = cap - 1;
    memcpy(out, src, n);
    out[n] = 0;
}

static bool make_root(void)
{
    char tmp[sizeof g_root];
    snprintf(tmp, sizeof tmp, "%s", g_root);
    for (char *p = tmp + 1; *p; p++)
        if (host_is_dir_sep(*p) && p[-1] != ':') { /* not "C:" on its own */
            char sep = *p;
            *p = 0;
            host_mkdir(tmp, 0777);
            *p = sep;
        }
    return host_mkdir(tmp, 0777) == 0 || errno == EEXIST;
}

static bool dir_path(const SceUtilitySavedataParam *p, const char *save, char *out, size_t cap)
{
    char game[16], dir[48];
    field(game, sizeof game, p->gameName, sizeof p->gameName);
    snprintf(dir, sizeof dir, "%s%s", game, save);
    if (!name_ok(dir))
        return false;
    snprintf(out, cap, "%s/%s", g_root, dir);
    return true;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_system_file(const char *name)
{
    static const char *const k[] = {"PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "ICON1.PNG", "PIC1.PNG", "SND0.AT3"};
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (!strcasecmp(name, k[i]))
            return true;
    return false;
}

static int64_t file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) ? (int64_t)st.st_size : -1;
}

static void psp_time(time_t t, ScePspDateTime *out)
{
    struct tm tm;
    host_localtime(&t, &tm);
    out->year = (uint16_t)(tm.tm_year + 1900);
    out->month = (uint16_t)(tm.tm_mon + 1);
    out->day = (uint16_t)tm.tm_mday;
    out->hour = (uint16_t)tm.tm_hour;
    out->minute = (uint16_t)tm.tm_min;
    out->second = (uint16_t)tm.tm_sec;
    out->microsecond = 0;
}

/* ---- PARAM.SFO ---- */

typedef struct Sfo {
    char title[0x80], savedata_title[0x80], detail[0x400], directory[0x40];
    int32_t parental;
    uint8_t file_list[SFO_FILE_LIST_SIZE];
    uint8_t params[SFO_PARAMS_SIZE];
} Sfo;

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static uint32_t get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t get32(const uint8_t *p) { return get16(p) | get16(p + 2) << 16; }

static bool sfo_read(const char *dir, Sfo *sfo)
{
    memset(sfo, 0, sizeof *sfo);
    char path[1200];
    snprintf(path, sizeof path, "%s/PARAM.SFO", dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    static uint8_t buf[0x4000];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    if (n < 20 || memcmp(buf, "\0PSF", 4))
        return false;
    uint32_t keys = get32(buf + 8), data = get32(buf + 12), count = get32(buf + 16);
    for (uint32_t i = 0; i < count && 20 + 16 * (i + 1) <= n; i++) {
        const uint8_t *e = buf + 20 + 16 * i;
        uint32_t ko = keys + get16(e), fmt = get16(e + 2), len = get32(e + 4), off = data + get32(e + 12);
        if (ko >= n || off + len > n)
            continue;
        const char *key = (const char *)buf + ko;
        const uint8_t *val = buf + off;
        struct { const char *key; void *dst; size_t cap; } k[] = {
            {"TITLE", sfo->title, sizeof sfo->title},
            {"SAVEDATA_TITLE", sfo->savedata_title, sizeof sfo->savedata_title},
            {"SAVEDATA_DETAIL", sfo->detail, sizeof sfo->detail},
            {"SAVEDATA_DIRECTORY", sfo->directory, sizeof sfo->directory},
            {"SAVEDATA_FILE_LIST", sfo->file_list, sizeof sfo->file_list},
            {"SAVEDATA_PARAMS", sfo->params, sizeof sfo->params},
        };
        if (!strncmp(key, "PARENTAL_LEVEL", n - ko) && fmt == 0x0404 && len >= 4) {
            sfo->parental = (int32_t)get32(val);
            continue;
        }
        for (size_t j = 0; j < sizeof k / sizeof k[0]; j++)
            if (!strncmp(key, k[j].key, n - ko)) {
                size_t c = len < k[j].cap ? len : k[j].cap;
                if (fmt == 0x0204 && c == k[j].cap)
                    c--; /* keep strings terminated */
                memcpy(k[j].dst, val, c);
            }
    }
    return true;
}

/* Keys in the order the PSP writes them (sorted). */
static bool sfo_write(const char *dir, const Sfo *sfo)
{
    struct { const char *key; uint16_t fmt; uint32_t len, max; const void *val; int32_t num; } e[] = {
        {"CATEGORY", 0x0204, 3, 4, "MS", 0},
        {"PARENTAL_LEVEL", 0x0404, 4, 4, NULL, sfo->parental},
        {"SAVEDATA_DETAIL", 0x0204, (uint32_t)strlen(sfo->detail) + 1, 0x400, sfo->detail, 0},
        {"SAVEDATA_DIRECTORY", 0x0204, (uint32_t)strlen(sfo->directory) + 1, 0x40, sfo->directory, 0},
        {"SAVEDATA_FILE_LIST", 0x0004, SFO_FILE_LIST_SIZE, SFO_FILE_LIST_SIZE, sfo->file_list, 0},
        {"SAVEDATA_PARAMS", 0x0004, SFO_PARAMS_SIZE, SFO_PARAMS_SIZE, sfo->params, 0},
        {"SAVEDATA_TITLE", 0x0204, (uint32_t)strlen(sfo->savedata_title) + 1, 0x80, sfo->savedata_title, 0},
        {"TITLE", 0x0204, (uint32_t)strlen(sfo->title) + 1, 0x80, sfo->title, 0},
    };
    enum { N = sizeof e / sizeof e[0] };
    static uint8_t buf[0x4000];
    memset(buf, 0, sizeof buf);
    uint32_t key_start = 20 + 16 * N, key_len = 0;
    for (int i = 0; i < N; i++)
        key_len += (uint32_t)strlen(e[i].key) + 1;
    uint32_t data_start = (key_start + key_len + 3) & ~3u, ko = 0, dof = 0;
    memcpy(buf, "\0PSF", 4);
    put32(buf + 4, 0x101);
    put32(buf + 8, key_start);
    put32(buf + 12, data_start);
    put32(buf + 16, N);
    for (int i = 0; i < N; i++) {
        uint8_t *h = buf + 20 + 16 * i;
        put16(h, ko);
        put16(h + 2, e[i].fmt);
        put32(h + 4, e[i].len);
        put32(h + 8, e[i].max);
        put32(h + 12, dof);
        strcpy((char *)buf + key_start + ko, e[i].key);
        ko += (uint32_t)strlen(e[i].key) + 1;
        if (e[i].val)
            memcpy(buf + data_start + dof, e[i].val, e[i].len);
        else
            put32(buf + data_start + dof, (uint32_t)e[i].num);
        dof += e[i].max;
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/PARAM.SFO", dir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = fwrite(buf, 1, data_start + dof, f) == data_start + dof;
    return fclose(f) == 0 && ok;
}

static bool sfo_has_file(const Sfo *sfo, const char *name)
{
    for (int i = 0; i < SFO_FILE_LIST_SIZE / 0x20; i++)
        if (!strncmp((const char *)sfo->file_list + 0x20 * i, name, 13) && sfo->file_list[0x20 * i])
            return true;
    return false;
}

static void sfo_set_file(Sfo *sfo, const char *name, bool secure)
{
    int free_slot = -1;
    for (int i = 0; i < SFO_FILE_LIST_SIZE / 0x20; i++) {
        uint8_t *e = sfo->file_list + 0x20 * i;
        if (!e[0]) {
            if (free_slot < 0)
                free_slot = i;
        } else if (!strncmp((const char *)e, name, 13)) {
            if (!secure)
                memset(e, 0, 0x20);
            return;
        }
    }
    if (secure && free_slot >= 0)
        strncpy((char *)sfo->file_list + 0x20 * free_slot, name, 12);
}

/* ---- the operations ---- */

static bool is_secure_mode(int mode)
{
    return mode == PSP_UTILITY_SAVEDATA_AUTOSAVE || mode == PSP_UTILITY_SAVEDATA_SAVE ||
           mode == PSP_UTILITY_SAVEDATA_LISTSAVE || mode == PSP_UTILITY_SAVEDATA_MAKEDATASECURE ||
           mode == PSP_UTILITY_SAVEDATA_WRITESECURE;
}

/* The save the dialog modes work on: saveName, or for the list modes the first entry of
 * saveNameList that exists (loading) or the first one at all (saving). */
static void pick_save(const SceUtilitySavedataParam *p, bool must_exist, char *out, size_t cap)
{
    field(out, cap, p->saveName, sizeof p->saveName);
    bool list = p->mode == PSP_UTILITY_SAVEDATA_LISTLOAD || p->mode == PSP_UTILITY_SAVEDATA_LISTSAVE ||
                p->mode == PSP_UTILITY_SAVEDATA_LISTDELETE;
    if (!list || !p->saveNameList)
        return;
    for (int i = 0; p->saveNameList[i][0]; i++) {
        char path[1200];
        if (!must_exist || (dir_path(p, p->saveNameList[i], path, sizeof path) && is_dir(path))) {
            field(out, cap, p->saveNameList[i], 20);
            return;
        }
    }
}

static int32_t file_path(const SceUtilitySavedataParam *p, const char *dir, char *out, size_t cap)
{
    char name[16];
    field(name, sizeof name, p->fileName, sizeof p->fileName);
    if (!name_ok(name))
        return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    snprintf(out, cap, "%s/%s", dir, name);
    return 0;
}

static int32_t do_load(SceUtilitySavedataParam *p, bool dialog)
{
    char save[24], dir[1100], path[1200];
    pick_save(p, true, save, sizeof save);
    if (!dir_path(p, save, dir, sizeof dir))
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_LOAD_PARAM : SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    if (!is_dir(dir))
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_LOAD_NO_DATA : SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
    int32_t r = file_path(p, dir, path, sizeof path);
    if (r)
        return r;
    FILE *f = fopen(path, "rb");
    if (!f)
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_LOAD_DATA_BROKEN : SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND;
    size_t n = p->dataBuf ? fread(p->dataBuf, 1, p->dataBufSize, f) : 0;
    fclose(f);
    p->dataSize = (SceSize)n;
    Sfo sfo;
    if (sfo_read(dir, &sfo)) {
        memcpy(p->sfoParam.title, sfo.title, sizeof p->sfoParam.title);
        memcpy(p->sfoParam.savedataTitle, sfo.savedata_title, sizeof p->sfoParam.savedataTitle);
        memcpy(p->sfoParam.detail, sfo.detail, sizeof p->sfoParam.detail);
        p->sfoParam.parentalLevel = (uint8_t)sfo.parental;
    }
    if (p->mode == PSP_UTILITY_SAVEDATA_LISTLOAD || p->mode == PSP_UTILITY_SAVEDATA_LISTSAVE)
        memcpy(p->saveName, save, sizeof p->saveName);
    return 0;
}

static bool write_blob(const char *dir, const char *name, const void *data, size_t size)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return false;
    bool ok = !size || fwrite(data, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static int32_t do_save(SceUtilitySavedataParam *p, bool dialog, bool create)
{
    char save[24], dir[1100], path[1200];
    pick_save(p, false, save, sizeof save);
    int32_t bad = dialog ? SCE_UTILITY_SAVEDATA_ERROR_SAVE_PARAM : SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    if (!dir_path(p, save, dir, sizeof dir))
        return bad;
    bool exists = is_dir(dir);
    if (!exists && !create)
        return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
    if (!exists && (!make_root() || host_mkdir(dir, 0777) != 0))
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_SAVE_ACCESS_ERROR : SCE_UTILITY_SAVEDATA_ERROR_RW_ACCESS_ERROR;
    int32_t r = file_path(p, dir, path, sizeof path);
    if (r)
        return r;
    char name[16];
    field(name, sizeof name, p->fileName, sizeof p->fileName);
    if (!write_blob(dir, name, p->dataBuf, p->dataBuf ? p->dataSize : 0))
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_SAVE_ACCESS_ERROR : SCE_UTILITY_SAVEDATA_ERROR_RW_ACCESS_ERROR;

    Sfo sfo;
    sfo_read(dir, &sfo);
    /* The SFO strings come from the param on every save that carries them (the MAKE and
     * dialog modes always do; a WRITE keeps the old ones when they are empty). */
    if (p->sfoParam.title[0] || !exists) {
        field(sfo.title, sizeof sfo.title, p->sfoParam.title, sizeof p->sfoParam.title);
        field(sfo.savedata_title, sizeof sfo.savedata_title, p->sfoParam.savedataTitle,
              sizeof p->sfoParam.savedataTitle);
        field(sfo.detail, sizeof sfo.detail, p->sfoParam.detail, sizeof p->sfoParam.detail);
        sfo.parental = p->sfoParam.parentalLevel;
    }
    const char *slash = strrchr(dir, '/');
    snprintf(sfo.directory, sizeof sfo.directory, "%.63s", slash ? slash + 1 : dir);
    sfo_set_file(&sfo, name, is_secure_mode(p->mode));
    if (!sfo_write(dir, &sfo))
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_SAVE_ACCESS_ERROR : SCE_UTILITY_SAVEDATA_ERROR_RW_ACCESS_ERROR;

    const struct { const PspUtilitySavedataFileData *d; const char *name; } extra[] = {
        {&p->icon0FileData, "ICON0.PNG"}, {&p->icon1FileData, "ICON1.PMF"},
        {&p->pic1FileData, "PIC1.PNG"}, {&p->snd0FileData, "SND0.AT3"},
    };
    for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++)
        if (extra[i].d->buf && extra[i].d->size)
            write_blob(dir, extra[i].name, extra[i].d->buf, extra[i].d->size);
    if (p->mode == PSP_UTILITY_SAVEDATA_LISTSAVE)
        memcpy(p->saveName, save, sizeof p->saveName);
    return 0;
}

static int32_t do_files(SceUtilitySavedataParam *p)
{
    char save[24], dir[1100];
    field(save, sizeof save, p->saveName, sizeof p->saveName);
    if (!dir_path(p, save, dir, sizeof dir))
        return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    SceUtilitySavedataFileListInfo *fl = p->fileList;
    if (fl)
        fl->resultNumSecureEntries = fl->resultNumNormalEntries = fl->resultNumSystemEntries = 0;
    if (!is_dir(dir))
        return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
    if (!fl)
        return 0;
    Sfo sfo;
    sfo_read(dir, &sfo);
    DIR *d = opendir(dir);
    if (!d)
        return SCE_UTILITY_SAVEDATA_ERROR_RW_ACCESS_ERROR;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || strlen(de->d_name) > 12)
            continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        uint32_t *num, max;
        SceUtilitySavedataFileListEntry *list;
        if (is_system_file(de->d_name)) {
            num = &fl->resultNumSystemEntries, max = fl->maxSystemEntries, list = fl->systemEntries;
        } else if (sfo_has_file(&sfo, de->d_name)) {
            num = &fl->resultNumSecureEntries, max = fl->maxSecureEntries, list = fl->secureEntries;
        } else {
            num = &fl->resultNumNormalEntries, max = fl->maxNormalEntries, list = fl->normalEntries;
        }
        if (!list || *num >= max)
            continue;
        SceUtilitySavedataFileListEntry *e = &list[(*num)++];
        memset(e, 0, sizeof *e);
        e->stat.st_mode = FIO_S_IFREG | 0x1FF;
        e->stat.st_attr = FIO_SO_IFREG;
        e->stat.st_size = st.st_size;
        psp_time(st.st_ctime, &e->stat.sce_ctime);
        psp_time(st.st_atime, &e->stat.sce_atime);
        psp_time(st.st_mtime, &e->stat.sce_mtime);
        snprintf(e->name, sizeof e->name, "%.15s", de->d_name);
    }
    closedir(d);
    return 0;
}

static int64_t dir_bytes(const char *dir)
{
    int64_t total = 0;
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        int64_t n = de->d_name[0] == '.' ? -1 : file_size(path);
        if (n > 0)
            total += n;
    }
    closedir(d);
    return total;
}

static void kb_string(char *out, size_t cap, int64_t kb)
{
    if (kb >= 1024 * 1024)
        snprintf(out, cap, "%d GB", (int)(kb / (1024 * 1024)));
    else if (kb >= 1024)
        snprintf(out, cap, "%d MB", (int)(kb / 1024));
    else
        snprintf(out, cap, "%d KB", (int)kb);
}

static int32_t do_sizes(SceUtilitySavedataParam *p)
{
    char save[24], dir[1100];
    field(save, sizeof save, p->saveName, sizeof p->saveName);
    bool have = dir_path(p, save, dir, sizeof dir) && is_dir(dir);
    if (p->msFree) {
        p->msFree->clusterSize = 0x8000;
        p->msFree->freeClusters = FREE_KB / 32;
        p->msFree->freeSpaceKB = FREE_KB;
        kb_string(p->msFree->freeSpaceStr, sizeof p->msFree->freeSpaceStr, FREE_KB);
    }
    if (p->msData) {
        int64_t kb = have ? (dir_bytes(dir) + 1023) / 1024 : 0;
        p->msData->usedClusters = (int32_t)((kb + 31) / 32);
        p->msData->usedSpaceKB = (int32_t)kb;
        kb_string(p->msData->usedSpaceStr, sizeof p->msData->usedSpaceStr, kb);
        p->msData->usedSpace32KB = p->msData->usedClusters * 32;
        kb_string(p->msData->usedSpace32Str, sizeof p->msData->usedSpace32Str, p->msData->usedSpace32KB);
    }
    if (p->utilityData) {
        int64_t kb = (p->dataSize + 0x8000 + 1023) / 1024; /* the data and the SFO/icons */
        p->utilityData->usedClusters = (int32_t)((kb + 31) / 32);
        p->utilityData->usedSpaceKB = (int32_t)kb;
        kb_string(p->utilityData->usedSpaceStr, sizeof p->utilityData->usedSpaceStr, kb);
        p->utilityData->usedSpace32KB = p->utilityData->usedClusters * 32;
        kb_string(p->utilityData->usedSpace32Str, sizeof p->utilityData->usedSpace32Str,
                  p->utilityData->usedSpace32KB);
    }
    return p->msData && !have ? SCE_UTILITY_SAVEDATA_ERROR_SIZES_NO_DATA : 0;
}

static int32_t do_getsize(SceUtilitySavedataParam *p)
{
    char save[24], dir[1100];
    field(save, sizeof save, p->saveName, sizeof p->saveName);
    bool have = dir_path(p, save, dir, sizeof dir) && is_dir(dir);
    SceUtilitySavedataSizeInfo *si = p->sizeInfo;
    if (si) {
        int64_t need = 0;
        for (int i = 0; si->secureEntries && i < si->numSecureEntries; i++)
            need += (int64_t)si->secureEntries[i].size;
        for (int i = 0; si->normalEntries && i < si->numNormalEntries; i++)
            need += (int64_t)si->normalEntries[i].size;
        si->sectorSize = 0x8000;
        si->freeSectors = FREE_KB / 32;
        si->freeKB = FREE_KB;
        kb_string(si->freeString, sizeof si->freeString, FREE_KB);
        si->neededKB = (int32_t)((need + 0x8000 + 1023) / 1024);
        kb_string(si->neededString, sizeof si->neededString, si->neededKB);
        si->overwriteKB = have ? (int32_t)((dir_bytes(dir) + 1023) / 1024) : 0;
        kb_string(si->overwriteString, sizeof si->overwriteString, si->overwriteKB);
    }
    return have ? 0 : SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
}

static int32_t do_list(SceUtilitySavedataParam *p)
{
    SceUtilitySavedataIdListInfo *il = p->idList;
    if (!il)
        return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    il->resultCount = 0;
    char game[16], save[24], prefix[48];
    field(game, sizeof game, p->gameName, sizeof p->gameName);
    field(save, sizeof save, p->saveName, sizeof p->saveName);
    /* "<>" in saveName matches anything from there on. */
    char *wild = strstr(save, "<>");
    if (wild)
        *wild = 0;
    snprintf(prefix, sizeof prefix, "%s%s", game, save);
    DIR *d = opendir(g_root);
    if (!d)
        return 0;
    struct dirent *de;
    while ((de = readdir(d)) && il->resultCount < il->maxCount) {
        if (de->d_name[0] == '.' || strncmp(de->d_name, prefix, strlen(prefix)) ||
            (!wild && strcmp(de->d_name, prefix)))
            continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", g_root, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        SceUtilitySavedataIdListEntry *e = &il->entries[il->resultCount++];
        memset(e, 0, sizeof *e);
        e->st_mode = FIO_S_IFDIR | 0x1FF;
        psp_time(st.st_ctime, &e->sce_ctime);
        psp_time(st.st_atime, &e->sce_atime);
        psp_time(st.st_mtime, &e->sce_mtime);
        snprintf(e->name, sizeof e->name, "%s", de->d_name + strlen(game));
    }
    closedir(d);
    return 0;
}

static int32_t do_erase(SceUtilitySavedataParam *p)
{
    char save[24], dir[1100], path[1200];
    field(save, sizeof save, p->saveName, sizeof p->saveName);
    if (!dir_path(p, save, dir, sizeof dir))
        return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    if (!is_dir(dir))
        return SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
    int32_t r = file_path(p, dir, path, sizeof path);
    if (r)
        return r;
    if (remove(path) != 0)
        return SCE_UTILITY_SAVEDATA_ERROR_RW_FILE_NOT_FOUND;
    Sfo sfo;
    if (sfo_read(dir, &sfo)) {
        char name[16];
        field(name, sizeof name, p->fileName, sizeof p->fileName);
        sfo_set_file(&sfo, name, false);
        sfo_write(dir, &sfo);
    }
    return 0;
}

static int32_t do_delete(SceUtilitySavedataParam *p, bool dialog)
{
    char save[24], dir[1100];
    pick_save(p, true, save, sizeof save);
    if (!dir_path(p, save, dir, sizeof dir))
        return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    if (!is_dir(dir))
        return dialog ? SCE_UTILITY_SAVEDATA_ERROR_DELETE_NO_DATA : SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA;
    DIR *d = opendir(dir);
    struct dirent *de;
    while (d && (de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        char path[1400];
        snprintf(path, sizeof path, "%s/%s", dir, de->d_name);
        remove(path);
    }
    if (d)
        closedir(d);
    return rmdir(dir) == 0 ? 0 : SCE_UTILITY_SAVEDATA_ERROR_RW_ACCESS_ERROR;
}

static int32_t run(SceUtilitySavedataParam *p)
{
    switch (p->mode) {
    case PSP_UTILITY_SAVEDATA_AUTOLOAD:
    case PSP_UTILITY_SAVEDATA_LOAD:
    case PSP_UTILITY_SAVEDATA_LISTLOAD:
        return do_load(p, true);
    case PSP_UTILITY_SAVEDATA_READSECURE:
    case PSP_UTILITY_SAVEDATA_READ:
        return do_load(p, false);
    case PSP_UTILITY_SAVEDATA_AUTOSAVE:
    case PSP_UTILITY_SAVEDATA_SAVE:
    case PSP_UTILITY_SAVEDATA_LISTSAVE:
        return do_save(p, true, true);
    case PSP_UTILITY_SAVEDATA_MAKEDATASECURE:
    case PSP_UTILITY_SAVEDATA_MAKEDATA:
        return do_save(p, false, true);
    case PSP_UTILITY_SAVEDATA_WRITESECURE:
    case PSP_UTILITY_SAVEDATA_WRITE:
        return do_save(p, false, false);
    case PSP_UTILITY_SAVEDATA_ERASESECURE:
    case PSP_UTILITY_SAVEDATA_ERASE:
        return do_erase(p);
    case PSP_UTILITY_SAVEDATA_LISTDELETE:
    case PSP_UTILITY_SAVEDATA_LISTALLDELETE:
    case PSP_UTILITY_SAVEDATA_AUTODELETE:
    case PSP_UTILITY_SAVEDATA_DELETE:
        return do_delete(p, true);
    case PSP_UTILITY_SAVEDATA_DELETEDATA:
        return do_delete(p, false);
    case PSP_UTILITY_SAVEDATA_SIZES:
        return do_sizes(p);
    case PSP_UTILITY_SAVEDATA_LIST:
        return do_list(p);
    case PSP_UTILITY_SAVEDATA_FILES:
        return do_files(p);
    case PSP_UTILITY_SAVEDATA_GETSIZE:
        return do_getsize(p);
    default:
        return SCE_UTILITY_SAVEDATA_ERROR_RW_BAD_PARAMS;
    }
}

/* ---- the calls ---- */

int32_t sceUtilitySavedataInitStart(SceUtilitySavedataParam *params)
{
    if (g_status != PSP_UTILITY_STATUS_NONE || !params) {
        PSP_TRACE(SCE_ERROR_UTILITY_INVALID_STATUS, "%p (busy)", (void *)params);
        return SCE_ERROR_UTILITY_INVALID_STATUS;
    }
    g_param = params;
    params->base.result = run(params);
    g_status = PSP_UTILITY_STATUS_INIT;
    char game[16], save[24], file[16];
    field(game, sizeof game, params->gameName, sizeof params->gameName);
    field(save, sizeof save, params->saveName, sizeof params->saveName);
    field(file, sizeof file, params->fileName, sizeof params->fileName);
    PSP_TRACE(0, "mode %d, %s%s/%s, %u bytes -> result 0x%08X", params->mode, game, save, file,
              (unsigned)params->dataSize, (unsigned)params->base.result);
    return 0;
}

int32_t sceUtilitySavedataGetStatus(void)
{
    int32_t s = g_status;
    if (g_status == PSP_UTILITY_STATUS_INIT)
        g_status = PSP_UTILITY_STATUS_VISIBLE;
    else if (g_status == PSP_UTILITY_STATUS_FINISHED) {
        g_status = PSP_UTILITY_STATUS_NONE;
        g_param = NULL;
    }
    return s;
}

int32_t sceUtilitySavedataUpdate(int unknown)
{
    if (g_status == PSP_UTILITY_STATUS_VISIBLE)
        g_status = PSP_UTILITY_STATUS_QUIT;
    return 0;
}

int32_t sceUtilitySavedataShutdownStart(void)
{
    if (g_status != PSP_UTILITY_STATUS_QUIT)
        return SCE_ERROR_UTILITY_INVALID_STATUS;
    g_status = PSP_UTILITY_STATUS_FINISHED;
    return 0;
}
