/* See savedata.h. Hand-written from:
 *   CUtility: the param setup FUN_08b25354 / FUN_08b24da4 / FUN_08b25090 and the
 *     InitStart helpers FUN_08b2448c (FILES), FUN_08b243b0 (READ), FUN_08b2409c
 *     (MAKEDATA), FUN_08b241a0 (WRITE), FUN_08b24770 (GETSIZE); Update FUN_08b25a98.
 *   CSaveMngr: Initialize FUN_08a64838, UpdateUT FUN_08a65b54 (states 0x16-0x22, 0x2B),
 *     the result tables at 0x08c89d98 (FUN_08a63dd8 / FUN_08a68330), the file list
 *     FUN_08a672f8 / FUN_08a641dc, BeginDataSave FUN_08a63290.
 *   CSystemMngr: GetSaveSize FUN_08ab8114, the save names FUN_08ab82f0, the save
 *     buffer FUN_08abb680, the version check FUN_08ab8604, and the load that unpacks
 *     a game save (FUN_088b7xxx at 0x088b7590).
 *   CCarnageHeart: the blocks' constructors FUN_0889c0c0 (system), FUN_0889c92c
 *     (shared), FUN_08ad321c (team card), FUN_0889d794 (battle). */
#include "savedata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "gamedata.h"
#include "packfile.h"
#include "psp_imports.h"
#include "psp_shim.h"

SaveState g_save;

/* ---- strings and names in the EBOOT (splat addresses, read at run time) ---- */

#define STR_GAME_TITLE_PTR 0x004818C8u  /* "CARNAGE HEART EXA" (SFO title) */
#define STR_SYSTEM_DATA 0x00437D40u
#define STR_BATTLE_DATA 0x00437D64u
#define STR_PLAYER_NAME_PTR 0x004879B0u
#define STR_STORY_OPEN_PTR 0x004879ACu  /* "Story Mode: Not Completed" */
#define STR_STORY_DONE_PTR 0x004879A8u
#define STR_PLAY_TIME 0x00437ECCu       /* the play-time line, a format with three %d */
#define ICON_PATHS 0x004818D4u          /* parts/icon/icon0_*.png by save type */
#define PARENTAL_LEVEL 0x00423904u      /* DAT_08c27904, a byte */
#define ARENA_NAMES 0x0044D8F0u         /* as battle_menu.c */
#define ARENA_RECORDS 0x0044DC10u
#define ARENA_STRIDE 0x3C0u

enum { SAVE_TYPE_BATTLE = 2, SAVE_TYPE_SYSTEM = 4 };

static const char *ptr_string(uint32_t ptr_addr)
{
    uint32_t p = gamedata_word(ptr_addr, 0);
    if (p >= 0x08804000u)
        p -= 0x08804000u;
    return p ? gamedata_string(p) : NULL;
}

static const char *or_empty(const char *s) { return s ? s : ""; }

/* ---- the blocks ---- */

static void put_word(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void put_half(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static uint32_t get_word(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

void savedata_new_system(void)
{
    uint8_t *s = g_save.system;
    memset(s, 0, SYSTEM_DATA_SIZE);
    put_word(s + 0x00, 0x3F800000); /* 1.0f */
    put_word(s + 0x04, 0x3F800000);
    put_word(s + 0x0C, 5);
    put_word(s + 0x10, 0xFFFFFFFF);
}

/* FUN_08ad321c */
static void new_team_card(uint8_t *t)
{
    memset(t, 0, TEAM_CARD_SIZE);
    put_half(t + 0x320, 1);
    put_half(t + 0x322, 1);
    put_half(t + 0x324, 1);
}

void savedata_new_game(void)
{
    GameDataBase *db = &g_save.db;
    memset(db, 0, sizeof *db); /* the cards: FUN_089eea80 clears their words and names */
    for (int i = 0; i < SAVE_TEAMS; i++)
        new_team_card(db->team[i]);
    uint8_t *b = (uint8_t *)db;
    put_word(b + 0x40C08 + 0x2C, 1);          /* FUN_0889c420 [0xB] */
    for (int i = 0; i < 2; i++) {             /* FUN_0889c51c: two team cards, word 0 = 1 */
        new_team_card(b + 0x40C80 + TEAM_CARD_SIZE * i);
        put_word(b + 0x40C80 + TEAM_CARD_SIZE * i, 1);
    }
    put_word(b + 0x40C40 + 0x6C0, 0x19);
    put_word(b + 0x44EE8, 1);
    put_word(b + 0x45068, 1);
    put_word(b + 0x45204, 1);
    memset(g_save.battle, 0, sizeof g_save.battle);
    put_word(g_save.battle + 0x4230, 0xFFFFFFFF);
    g_save.battle_file[0] = 0;
}

bool savedata_card_used(int card)
{
    return card >= 0 && card < SAVE_CARDS && get_word(g_save.db.card[card].header) != 0;
}

const OkeDataBase *savedata_card(int card)
{
    return card >= 0 && card < SAVE_CARDS ? &g_save.db.card[card] : NULL;
}

void savedata_card_name(int card, char *out, size_t n)
{
    out[0] = 0;
    if (card < 0 || card >= SAVE_CARDS || !n)
        return;
    const char *name = (const char *)g_save.db.card[card].header + 4;
    size_t len = strnlen(name, 0x1C);
    if (len >= n)
        len = n - 1;
    for (size_t i = 0; i < len; i++) /* the presets fill unused bytes with 0xCD/0xFF */
        out[i] = (unsigned char)name[i] < 0x20 || (unsigned char)name[i] >= 0x7F ? '?' : name[i];
    out[len] = 0;
}

bool savedata_story_completed(void)
{
    return get_word(g_save.system + 0x74) != 0;
}

/* ---- CUtility ---- */

static SceUtilitySavedataParam g_param;             /* CUtility + 0x648 */
static SceUtilitySavedataFileListInfo g_file_info;  /* + 0x1040 */
static SceUtilitySavedataFileListEntry g_file_entries[0x400]; /* the 0x14000-byte list */
static SceUtilitySavedataSizeInfo g_size_info;
static SceUtilitySavedataSizeEntry g_size_entry;
static PspUtilitySavedataListSaveNewData g_new_data;
static uint8_t g_icon[0x10000];
static long g_icon_size = -1;
static int g_icon_type = -1;
static bool g_util_busy;

static uint8_t g_buf[BATTLE_SAVE_SIZE];

static const char *save_name(int type) { return type == SAVE_TYPE_BATTLE ? "B" : ""; } /* FUN_08a617f8 */

/* FUN_08b25354 + FUN_08b24da4: the common header and the default file. */
static void param_begin(int type, int mode)
{
    memset(&g_param, 0, sizeof g_param);
    g_param.base.size = sizeof g_param;
    g_param.base.language = 1; /* CUtility +8 */
    g_param.base.buttonSwap = 0;
    g_param.base.graphicsThread = 0x11;
    g_param.base.accessThread = 0x13;
    g_param.base.fontThread = 0x12;
    g_param.base.soundThread = 0x10;
    g_param.mode = mode;
    snprintf(g_param.gameName, sizeof g_param.gameName, "%s", SAVE_GAME_NAME);
    snprintf(g_param.saveName, sizeof g_param.saveName, "%s", save_name(type));
    snprintf(g_param.fileName, sizeof g_param.fileName, "DATA.BIN");
    memcpy(g_param.key, "CHEXA", 5); /* padded with zeroes to 16 bytes */
    g_param.multiStatus = 1;
}

static bool util_start(void)
{
    int32_t r = sceUtilitySavedataInitStart(&g_param);
    if (r < 0) {
        SDL_Log("[save] sceUtilitySavedataInitStart failed: 0x%08X", (unsigned)r);
        g_param.base.result = r;
        return false;
    }
    g_util_busy = true;
    return true;
}

/* FUN_08b25a98 for the savedata dialog; true once the utility is done. */
static bool util_poll(void)
{
    if (!g_util_busy)
        return true;
    int32_t st = sceUtilitySavedataGetStatus();
    if (st == PSP_UTILITY_STATUS_VISIBLE)
        sceUtilitySavedataUpdate(1);
    else if (st == PSP_UTILITY_STATUS_QUIT)
        sceUtilitySavedataShutdownStart();
    else if (st == PSP_UTILITY_STATUS_NONE)
        g_util_busy = false;
    return !g_util_busy;
}

/* FUN_08b2448c: list the save's files. Secure: up to 99 (0x1EF0 bytes); normal: 0x400. */
static void util_files(int type, bool normal)
{
    param_begin(type, PSP_UTILITY_SAVEDATA_FILES);
    memset(&g_file_info, 0, sizeof g_file_info);
    if (normal) {
        g_file_info.maxNormalEntries = 0x400;
        g_file_info.normalEntries = g_file_entries;
    } else {
        g_file_info.maxSecureEntries = 99;
        g_file_info.secureEntries = g_file_entries;
    }
    g_param.fileList = &g_file_info;
    util_start();
}

/* FUN_08b243b0: secure 0xF, else 0x10. */
static void util_read(int type, const char *file, void *buf, SceSize size, bool secure)
{
    param_begin(type, secure ? PSP_UTILITY_SAVEDATA_READSECURE : PSP_UTILITY_SAVEDATA_READ);
    snprintf(g_param.fileName, sizeof g_param.fileName, "%s", file);
    g_param.dataBuf = buf;
    g_param.dataBufSize = g_param.dataSize = size;
    util_start();
}

/* The save's icon, FUN_08a619f8: parts/icon/icon0_*.png from the pack. */
static void load_icon(int type)
{
    if (g_icon_type == type)
        return;
    g_icon_type = type;
    g_icon_size = -1;
    const char *path = ptr_string(ICON_PATHS + 4u * (uint32_t)type);
    Pack pack;
    if (!path || pack_open(&pack, "disc0:/PSP_GAME/USRDIR") < 0)
        return;
    const PackEntry *e = pack_find(&pack, path);
    if (e)
        g_icon_size = pack_read(&pack, e, g_icon, sizeof g_icon);
    pack_close(&pack);
}

/* The EBOOT's format is only used when it takes exactly three ints. */
static bool three_ints(const char *fmt)
{
    int n = 0;
    for (const char *p = fmt; p && *p; p++)
        if (*p == '%') {
            if (p[1] == 'd')
                n++, p++;
            else
                return false;
        }
    return fmt && n == 3;
}

/* CSystemMngr::GetSaveGameDataName (FUN_08ab82f0) for types 4 and 2. */
static void sfo_strings(int type, char *title, size_t tn, char *detail, size_t dn)
{
    if (type == SAVE_TYPE_SYSTEM) {
        snprintf(title, tn, "%s", or_empty(gamedata_string(STR_SYSTEM_DATA)));
        /* FUN_08ab953c: play time from +0x78, in seconds. */
        uint32_t t = get_word(g_save.system + 0x78);
        const char *fmt = gamedata_string(STR_PLAY_TIME);
        detail[0] = 0;
        if (three_ints(fmt))
            snprintf(detail, dn, fmt, (int)(t / 3600), (int)(t % 3600 / 60), (int)(t % 60));
        size_t n = strlen(detail);
        snprintf(detail + n, dn - n, "%s",
                 or_empty(ptr_string(savedata_story_completed() ? STR_STORY_DONE_PTR : STR_STORY_OPEN_PTR)));
    } else {
        snprintf(title, tn, "%s", or_empty(gamedata_string(STR_BATTLE_DATA)));
        /* "%s%s": "Player Name:" and the name at the battle block's start. */
        char name[17];
        memcpy(name, g_save.battle, 16);
        name[16] = 0;
        snprintf(detail, dn, "%s%s", or_empty(ptr_string(STR_PLAYER_NAME_PTR)), name);
    }
}

/* FUN_08b2409c / FUN_08b241a0 with FUN_08b25090: create or write, with the SFO
 * strings and icon. Secure for the system data (param_4 0), normal for game data. */
static void util_write(int type, const char *file, void *buf, SceSize size, bool create, bool secure)
{
    param_begin(type, create ? (secure ? PSP_UTILITY_SAVEDATA_MAKEDATASECURE : PSP_UTILITY_SAVEDATA_MAKEDATA)
                             : (secure ? PSP_UTILITY_SAVEDATA_WRITESECURE : PSP_UTILITY_SAVEDATA_WRITE));
    snprintf(g_param.fileName, sizeof g_param.fileName, "%s", file);
    g_param.dataBuf = buf;
    g_param.dataBufSize = g_param.dataSize = size;
    g_param.overwrite = 1;
    snprintf(g_param.sfoParam.title, sizeof g_param.sfoParam.title, "%s",
             or_empty(ptr_string(STR_GAME_TITLE_PTR)));
    sfo_strings(type, g_param.sfoParam.savedataTitle, sizeof g_param.sfoParam.savedataTitle,
                g_param.sfoParam.detail, sizeof g_param.sfoParam.detail);
    g_param.sfoParam.parentalLevel = (uint8_t)gamedata_word(PARENTAL_LEVEL, 1);
    load_icon(type);
    if (g_icon_size > 0) {
        g_param.icon0FileData.buf = g_icon;
        g_param.icon0FileData.bufSize = g_param.icon0FileData.size = (SceSize)g_icon_size;
    }
    memset(&g_new_data, 0, sizeof g_new_data);
    g_param.newData = &g_new_data;
    util_start();
}

/* FUN_08b24770: the space check. */
static void util_getsize(int type, const char *file, SceSize size, bool secure)
{
    param_begin(type, PSP_UTILITY_SAVEDATA_GETSIZE);
    snprintf(g_param.fileName, sizeof g_param.fileName, "%s", file);
    memset(&g_size_info, 0, sizeof g_size_info);
    memset(&g_size_entry, 0, sizeof g_size_entry);
    g_size_entry.size = size;
    snprintf(g_size_entry.name, sizeof g_size_entry.name, "%s", file);
    if (secure) {
        g_size_info.numSecureEntries = 1;
        g_size_info.secureEntries = &g_size_entry;
    } else {
        g_size_info.numNormalEntries = 1;
        g_size_info.normalEntries = &g_size_entry;
    }
    g_param.sizeInfo = &g_size_info;
    g_param.dataSize = size;
    util_start();
}

/* ---- CSaveMngr ---- */

typedef enum {
    SEQ_IDLE,
    SEQ_CHECK,  /* type 4, exec 3 */
    SEQ_LIST,   /* type 2, the list of an exec 1 */
    SEQ_LOAD,   /* type 2, state 0x2B */
    SEQ_SAVE,   /* type 2, BeginDataSave */
} SeqKind;

/* The UpdateUT states used here, numbered as in the game. */
enum {
    ST_CHECK_FILES = 0x16,
    ST_CHECK_READ = 0x17,
    ST_CHECK_SIZE = 0x18,
    ST_SYS_FILES = 0x20,
    ST_SYS_SIZE = 0x21,
    ST_SYS_WRITE = 0x22,
    ST_GAME_FILES = 0x00,
    ST_GAME_READ = 0x2B,
    ST_GAME_SIZE = 0x0E,
    ST_GAME_WRITE = 0x10,
    ST_DONE = -1,
};

static struct {
    SeqKind kind;
    int state;
    bool started;   /* the state's utility call was made */
    bool exists;    /* +0x50: FILES found the save */
    int secure_files; /* the FILES result's secure count (state 0x21 reads +0xC) */
    int32_t result;
    char file[SAVE_FILE_MAX];
    SaveListEntry list[SAVE_LIST_MAX];
    int list_count;
} g_seq;

static void seq_begin(SeqKind kind, int state)
{
    g_seq.kind = kind;
    g_seq.state = state;
    g_seq.started = false;
    g_seq.result = 0;
}

static void seq_goto(int state)
{
    g_seq.state = state;
    g_seq.started = false;
}

/* Runs the current state's call once and reports its result when it's done:
 * returns false while waiting. */
static bool seq_call(void (*start)(void), int32_t *result)
{
    if (!g_seq.started) {
        g_seq.started = true;
        start();
        return false;
    }
    if (!util_poll())
        return false;
    *result = g_param.base.result;
    return true;
}

/* -- the start-up check -- */

static uint8_t g_sys_buf[SYSTEM_DATA_SIZE];

static void call_sys_files(void) { util_files(SAVE_TYPE_SYSTEM, false); }
static void call_sys_read(void) { util_read(SAVE_TYPE_SYSTEM, "DATA.BIN", g_sys_buf, SYSTEM_DATA_SIZE, true); }
static void call_sys_size(void) { util_getsize(SAVE_TYPE_SYSTEM, "DATA.BIN", SYSTEM_DATA_SIZE, true); }
static void call_sys_write(void)
{
    /* FUN_08a63794 with type 4: the block itself, 0x7C bytes. */
    memcpy(g_sys_buf, g_save.system, SYSTEM_DATA_SIZE);
    util_write(SAVE_TYPE_SYSTEM, "DATA.BIN", g_sys_buf, SYSTEM_DATA_SIZE, !g_seq.exists || g_seq.secure_files == 0,
               true);
}

void savedata_check_start(void)
{
    savedata_new_system();
    seq_begin(SEQ_CHECK, ST_CHECK_FILES);
    SDL_Log("[save] start-up check (CSaveMngr type 4, exec 3): %s/%s", psp_savedata_root(), SAVE_GAME_NAME);
}

static int check_update(void)
{
    int32_t r;
    switch (g_seq.state) {
    case ST_CHECK_FILES: /* table 0x16: 0 -> 0x17, NO_DATA -> 0x18, else a message */
        if (!seq_call(call_sys_files, &r))
            return 1;
        if (r == 0) {
            seq_goto(ST_CHECK_READ);
        } else if (r == SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA) {
            SDL_Log("[save] no system data: creating it");
            seq_goto(ST_CHECK_SIZE);
        } else {
            SDL_Log("[save] system data check failed (0x%08X): going on with new data", (unsigned)r);
            g_seq.result = r;
            seq_goto(ST_DONE);
        }
        return 1;
    case ST_CHECK_READ: /* table 0x17: 0 -> done (the result copies the block in) */
        if (!seq_call(call_sys_read, &r))
            return 1;
        if (r == 0 && g_param.dataSize == SYSTEM_DATA_SIZE) {
            memcpy(g_save.system, g_sys_buf, SYSTEM_DATA_SIZE);
            SDL_Log("[save] system data loaded (Story Mode %s, play time %u s)",
                    savedata_story_completed() ? "completed" : "not completed",
                    (unsigned)get_word(g_save.system + 0x78));
        } else {
            SDL_Log("[save] system data unreadable (0x%08X, %u bytes): going on with new data", (unsigned)r,
                    (unsigned)g_param.dataSize);
            g_seq.result = r ? r : -1;
        }
        seq_goto(ST_DONE);
        return 1;
    case ST_CHECK_SIZE: /* 0x18: the space for a new save, then (0x51) the create path */
        if (!seq_call(call_sys_size, &r))
            return 1;
        seq_goto(ST_SYS_FILES);
        return 1;
    case ST_SYS_FILES: /* 0x20: 0 or NO_DATA -> 0x21 */
        if (!seq_call(call_sys_files, &r))
            return 1;
        g_seq.exists = r == 0;
        g_seq.secure_files = (int)g_file_info.resultNumSecureEntries;
        if (r != 0 && r != SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA) {
            g_seq.result = r;
            seq_goto(ST_DONE);
        } else {
            seq_goto(ST_SYS_SIZE);
        }
        return 1;
    case ST_SYS_SIZE: /* 0x21: no file yet -> create (+0x44 = 1), else write */
        if (!seq_call(call_sys_size, &r))
            return 1;
        seq_goto(ST_SYS_WRITE);
        return 1;
    case ST_SYS_WRITE: /* 0x22 */
        if (!seq_call(call_sys_write, &r))
            return 1;
        g_seq.result = r;
        if (r == 0)
            SDL_Log("[save] system data saved");
        else
            SDL_Log("[save] saving the system data failed: 0x%08X", (unsigned)r);
        seq_goto(ST_DONE);
        return 1;
    default:
        g_seq.kind = SEQ_IDLE;
        return 0;
    }
}

int savedata_check_update(void)
{
    return g_seq.kind == SEQ_CHECK ? check_update() : 0;
}

/* -- Battle Data -- */

static void call_game_files(void) { util_files(SAVE_TYPE_BATTLE, true); }
static void call_game_read(void) { util_read(SAVE_TYPE_BATTLE, g_seq.file, g_buf, BATTLE_SAVE_SIZE, true); }
static void call_game_size(void) { util_getsize(SAVE_TYPE_BATTLE, g_seq.file, BATTLE_SAVE_SIZE, false); }
static void call_game_write(void)
{
    /* FUN_08abb680, types 0-2: the version string without its NUL, the shared block,
     * then the mode's block. */
    size_t v = strlen(SAVE_VERSION);
    memcpy(g_buf, SAVE_VERSION, v);
    memcpy(g_buf + v, &g_save.db, GAME_DB_SIZE);
    memcpy(g_buf + v + GAME_DB_SIZE, g_save.battle, BATTLE_DATA_SIZE);
    util_write(SAVE_TYPE_BATTLE, g_seq.file, g_buf, BATTLE_SAVE_SIZE, !g_seq.exists, false);
}

/* FUN_08a641dc: at least five characters, ending in ".CHE". */
static bool is_game_file(const char *name)
{
    size_t n = strlen(name);
    return n >= 5 && !memcmp(name + n - 4, ".CHE", 4);
}

static void collect_list(void)
{
    g_seq.list_count = 0;
    for (uint32_t i = 0; i < g_file_info.resultNumNormalEntries && g_seq.list_count < SAVE_LIST_MAX; i++) {
        const SceUtilitySavedataFileListEntry *e = &g_file_entries[i];
        char name[17];
        memcpy(name, e->name, 16);
        name[16] = 0;
        if (!is_game_file(name) || strlen(name) >= SAVE_FILE_MAX)
            continue;
        SaveListEntry *l = &g_seq.list[g_seq.list_count++];
        snprintf(l->file, sizeof l->file, "%.12s", name);
        l->broken = e->stat.st_size != BATTLE_SAVE_SIZE;
        l->mtime = e->stat.sce_mtime;
    }
}

void savedata_list_start(void)
{
    g_seq.list_count = 0;
    seq_begin(SEQ_LIST, ST_GAME_FILES);
}

int savedata_list_update(void)
{
    if (g_seq.kind != SEQ_LIST)
        return 0;
    int32_t r;
    if (g_seq.state == ST_GAME_FILES) {
        if (!seq_call(call_game_files, &r))
            return 1;
        if (r == 0)
            collect_list();
        else if (r != SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA)
            g_seq.result = r;
        SDL_Log("[save] Battle Data: %d file(s) in %s/%sB", g_seq.list_count, psp_savedata_root(), SAVE_GAME_NAME);
        seq_goto(ST_DONE);
        return 1;
    }
    g_seq.kind = SEQ_IDLE;
    return 0;
}

int savedata_list(const SaveListEntry **out)
{
    *out = g_seq.list;
    return g_seq.list_count;
}

void savedata_load_start(const char *file)
{
    seq_begin(SEQ_LOAD, ST_GAME_READ);
    snprintf(g_seq.file, sizeof g_seq.file, "%s", file);
}

void savedata_save_start(const char *file)
{
    seq_begin(SEQ_SAVE, ST_GAME_FILES);
    snprintf(g_seq.file, sizeof g_seq.file, "%s", file);
}

/* The load's unpacking (0x088b7590..): skip the version, copy the shared block and the
 * mode's block. */
static bool unpack_game(void)
{
    size_t v = strlen(SAVE_VERSION);
    if (g_param.dataSize != BATTLE_SAVE_SIZE) {
        SDL_Log("[save] %s: %u bytes, expected %u", g_seq.file, (unsigned)g_param.dataSize,
                (unsigned)BATTLE_SAVE_SIZE);
        return false;
    }
    if (memcmp(g_buf, SAVE_VERSION, v)) /* FUN_08ab8604 compares, the game goes on anyway */
        SDL_Log("[save] %s: version \"%.6s\", expected \"%s\"", g_seq.file, (const char *)g_buf, SAVE_VERSION);
    memcpy(&g_save.db, g_buf + v, GAME_DB_SIZE);
    memcpy(g_save.battle, g_buf + v + GAME_DB_SIZE, BATTLE_DATA_SIZE);
    snprintf(g_save.battle_file, sizeof g_save.battle_file, "%s", g_seq.file);
    return true;
}

static int io_update(void)
{
    int32_t r;
    switch (g_seq.state) {
    case ST_GAME_READ:
        if (!seq_call(call_game_read, &r))
            return 1;
        if (r != 0 || !unpack_game()) {
            g_seq.result = r ? r : -1;
            SDL_Log("[save] loading %s failed (0x%08X)", g_seq.file, (unsigned)r);
        } else {
            int used = 0;
            for (int i = 0; i < SAVE_CARDS; i++)
                used += savedata_card_used(i);
            SDL_Log("[save] Battle Data %s loaded: %d OKE card(s)", g_seq.file, used);
        }
        seq_goto(ST_DONE);
        return 1;
    case ST_GAME_FILES:
        if (!seq_call(call_game_files, &r))
            return 1;
        g_seq.exists = r == 0;
        if (r != 0 && r != SCE_UTILITY_SAVEDATA_ERROR_RW_NO_DATA) {
            g_seq.result = r;
            seq_goto(ST_DONE);
        } else {
            seq_goto(ST_GAME_SIZE);
        }
        return 1;
    case ST_GAME_SIZE:
        if (!seq_call(call_game_size, &r))
            return 1;
        seq_goto(ST_GAME_WRITE);
        return 1;
    case ST_GAME_WRITE: /* BeginDataSave: McCreate when the save is new, else McSave */
        if (!seq_call(call_game_write, &r))
            return 1;
        g_seq.result = r;
        if (r == 0) {
            snprintf(g_save.battle_file, sizeof g_save.battle_file, "%s", g_seq.file);
            SDL_Log("[save] Battle Data saved as %s/%sB/%s", psp_savedata_root(), SAVE_GAME_NAME, g_seq.file);
        } else {
            SDL_Log("[save] saving %s failed: 0x%08X", g_seq.file, (unsigned)r);
        }
        seq_goto(ST_DONE);
        return 1;
    default:
        g_seq.kind = SEQ_IDLE;
        return 0;
    }
}

int savedata_io_update(void)
{
    return g_seq.kind == SEQ_LOAD || g_seq.kind == SEQ_SAVE ? io_update() : 0;
}

int32_t savedata_result(void)
{
    return g_seq.result;
}

int32_t savedata_run(int (*update)(void))
{
    for (int guard = 0; guard < 1000 && update(); guard++) {
    }
    return g_seq.result;
}

/* ---- the test save ---- */

static bool read_preset(Pack *pack, const char *path, OkeDataBase *out)
{
    const PackEntry *e = path ? pack_find(pack, path) : NULL;
    return e && pack_read(pack, e, out, sizeof *out) == (long)sizeof *out;
}

static void set_card_name(OkeDataBase *card, const char *name)
{
    memset(card->header + 4, 0, 0x1C);
    snprintf((char *)card->header + 4, 0x1C, "%s", name);
}

bool savedata_make_test_save(const char *file)
{
    Pack pack;
    if (pack_open(&pack, "disc0:/PSP_GAME/USRDIR") < 0) {
        SDL_Log("[save] test save: can't open the pack");
        return false;
    }
    savedata_new_game();
    memcpy(g_save.battle, "TESTER", 6); /* the player name */
    int n = 0;
    /* The two simulation OKEs, then the leaders of the first four arena teams. */
    static const char *const k_sim[2] = {"preset/oke_t01_1.bin", "preset/oke_t01_2.bin"};
    for (int i = 0; i < 2; i++)
        if (read_preset(&pack, k_sim[i], &g_save.db.card[n])) {
            char name[24];
            snprintf(name, sizeof name, "Simulation %d", i + 1);
            set_card_name(&g_save.db.card[n++], name);
        }
    for (int i = 0; i < 4; i++) {
        const char *path = gamedata_string(ARENA_RECORDS + (uint32_t)i * ARENA_STRIDE + 0x14u);
        if (!read_preset(&pack, path, &g_save.db.card[n]))
            continue;
        const char *name = ptr_string(ARENA_NAMES + 4u * (uint32_t)i);
        set_card_name(&g_save.db.card[n++], name ? name : "Arena OKE");
    }
    pack_close(&pack);
    if (n == 0) {
        SDL_Log("[save] test save: no preset OKEs could be read");
        return false;
    }
    savedata_save_start(file);
    int32_t r = savedata_run(savedata_io_update);
    SDL_Log("[save] test save %s with %d preset OKE card(s): %s", file, n, r == 0 ? "written" : "failed");
    return r == 0;
}
