/* See team_trade.h. Hand-written from CTeamFormationMngr's transfer modes, CSystemMngr's
 * CExportTeamData build / check / install and CTeamDataDockInfoWnd's counts. */
#include "team_trade.h"

#include <string.h>

#include <SDL3/SDL.h>

#include "pgchip_edit.h"

/* CExportTeamData (0x5FC0 bytes, FUN_088ef368 / FUN_088ef478):
 *   +0x00 "CETD", +4 0x1C, +8 the OKE cards in it, +0xC how many are manual,
 *   +0x10 16 zero bytes (+0x20 a zeroed 0x10-byte block), +0x30 the team card, whose
 *   members point at the file's cards 0-2, +0x370 / +0x2230 / +0x40F0 the three cards,
 *   +0x5FB0 the version ("0.0.44", 8 bytes), +0x5FB8 the sum of every byte before it. */
#define ETD_COUNT 0x08
#define ETD_MANUAL 0x0C
#define ETD_TEAM 0x30
#define ETD_CARD 0x370
#define ETD_VERSION 0x5FB0
#define ETD_SUM 0x5FB8

#define TEAM_STATUS 0x000
#define TEAM_MEMBER 0x290 /* three CFormationDataBase of 0x30; +0x20 the card */
#define MEMBER_SIZE 0x30
#define MEMBER_CARD 0x20
#define INSTALLED 2       /* FUN_089eea20(.., 2) */

_Static_assert(ETD_CARD + 3 * sizeof(OkeDataBase) == ETD_VERSION, "CExportTeamData layout");

static uint32_t get_word(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void put_word(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

/* FUN_08ad3204 / FUN_08ad31fc on member m. */
static int member_card(const uint8_t *team, int m)
{
    return (int32_t)get_word(team + TEAM_MEMBER + MEMBER_SIZE * m + MEMBER_CARD);
}

static void set_member_card(uint8_t *team, int m, int card)
{
    put_word(team + TEAM_MEMBER + MEMBER_SIZE * m + MEMBER_CARD, (uint32_t)card);
}

static bool valid_card(int card)
{
    return card >= 0 && card < SAVE_CARDS;
}

static int card_status(const OkeDataBase *c)
{
    return (int)get_word(c->header);
}

static void set_card_status(OkeDataBase *c, int status)
{
    put_word(c->header, (uint32_t)status);
}

/* ---- the counts ---- */

int team_trade_teams_used(void)
{
    int n = 0;
    for (int t = 0; t < SAVE_TEAMS; t++)
        n += get_word(g_save.db.team[t] + TEAM_STATUS) != 0;
    return n;
}

int team_trade_cards_used(void)
{
    int n = 0;
    for (int c = 0; c < SAVE_CARDS; c++)
        n += card_status(&g_save.db.card[c]) != 0;
    return n;
}

bool team_trade_full(void)
{
    return team_trade_teams_used() >= SAVE_TEAMS || team_trade_cards_used() >= SAVE_CARDS;
}

bool team_trade_has_members(const uint8_t *team)
{
    if (get_word(team + TEAM_STATUS) == 0)
        return false;
    for (int m = 0; m < TEAM_MEMBERS; m++)
        if (valid_card(member_card(team, m)))
            return true;
    return false;
}

int team_trade_distinct_cards(const uint8_t *team)
{
    if (get_word(team + TEAM_STATUS) == 0)
        return 0;
    int seen[TEAM_MEMBERS], n = 0;
    for (int m = 0; m < TEAM_MEMBERS; m++) {
        int c = member_card(team, m);
        if (!valid_card(c))
            continue;
        bool dup = false;
        for (int k = 0; k < n; k++)
            dup |= seen[k] == c;
        if (!dup)
            seen[n++] = c;
    }
    return n;
}

/* ---- installing ---- */

static int free_team(void)
{
    for (int t = 0; t < SAVE_TEAMS; t++)
        if (get_word(g_save.db.team[t] + TEAM_STATUS) == 0)
            return t;
    return -1;
}

static int free_card(void)
{
    for (int c = 0; c < SAVE_CARDS; c++)
        if (card_status(&g_save.db.card[c]) == 0)
            return c;
    return -1;
}

/* The second half of FUN_08ac6db8 and of FUN_08ab7b54: the team copied in, status 2;
 * each of its own cards (index k, 0-2, as `cards[k]`) once into a free card, status 2,
 * and every member that used it pointed there; a member on no card of its own keeps
 * its value. `local[m]` is the card index 0-2 member m uses, else -1 and `keep[m]`. */
static int install(const uint8_t *team, const OkeDataBase *const cards[TEAM_MEMBERS],
                   const int local[TEAM_MEMBERS], const int keep[TEAM_MEMBERS])
{
    int t = free_team();
    if (t < 0)
        return -1;
    uint8_t *dst = g_save.db.team[t];
    memcpy(dst, team, TEAM_CARD_SIZE);
    put_word(dst + TEAM_STATUS, INSTALLED);
    bool placed[TEAM_MEMBERS] = {false, false, false};
    for (int m = 0; m < TEAM_MEMBERS; m++) {
        int k = local[m];
        if (k < 0 || k >= TEAM_MEMBERS) {
            set_member_card(dst, m, keep[m]);
            continue;
        }
        if (placed[k])
            continue;
        placed[k] = true;
        int c = free_card();
        if (c < 0)
            continue; /* as the game: the member keeps the index it had */
        memcpy(&g_save.db.card[c], cards[k], sizeof(OkeDataBase));
        set_card_status(&g_save.db.card[c], INSTALLED);
        for (int j = 0; j < TEAM_MEMBERS; j++)
            if (local[j] == k)
                set_member_card(dst, j, c);
    }
    return t;
}

bool team_trade_install(const GameDataBase *from, int team)
{
    if (!from || team < 0 || team >= SAVE_TEAMS)
        return false;
    const uint8_t *src = from->team[team];
    int add = team_trade_distinct_cards(src);
    if (!team_trade_has_members(src) || team_trade_teams_used() + 1 > SAVE_TEAMS ||
        team_trade_cards_used() + add > SAVE_CARDS)
        return false;
    /* the members' cards, each once, in member order (the game's local[]) */
    const OkeDataBase *cards[TEAM_MEMBERS] = {NULL, NULL, NULL};
    int local[TEAM_MEMBERS], keep[TEAM_MEMBERS], n = 0;
    for (int m = 0; m < TEAM_MEMBERS; m++) {
        int c = member_card(src, m);
        keep[m] = c;
        local[m] = -1;
        if (!valid_card(c))
            continue;
        for (int j = 0; j < m; j++)
            if (member_card(src, j) == c)
                local[m] = local[j];
        if (local[m] < 0) {
            cards[n] = &from->card[c];
            local[m] = n++;
        }
    }
    int t = install(src, cards, local, keep);
    SDL_Log("[team] team %d of the other save installed as team %d, %d OKE card(s)", team + 1, t + 1, n);
    return t >= 0;
}

/* ---- CExportTeamData ---- */

static uint32_t byte_sum(const uint8_t *p, size_t n) /* FUN_08ab9508 */
{
    uint32_t s = 0;
    for (size_t i = 0; i < n; i++)
        s += p[i];
    return s;
}

bool team_trade_export(int team, uint8_t out[TEAM_FILE_SIZE])
{
    if (team < 0 || team >= SAVE_TEAMS)
        return false;
    const uint8_t *src = g_save.db.team[team];
    if (get_word(src + TEAM_STATUS) != 1 || !team_trade_has_members(src))
        return false;
    memset(out, 0, TEAM_FILE_SIZE);
    memcpy(out, "CETD", 4);
    put_word(out + 4, 0x1C);
    memcpy(out + ETD_TEAM, src, TEAM_CARD_SIZE);
    int used[TEAM_MEMBERS] = {-1, -1, -1}, count = 0, manual = 0;
    for (int m = 0; m < TEAM_MEMBERS; m++) {
        int c = member_card(src, m);
        if (!valid_card(c)) {
            set_member_card(out + ETD_TEAM, m, c);
            continue;
        }
        int at = -1;
        for (int k = 0; k < TEAM_MEMBERS; k++)
            if (used[k] == c)
                at = k;
        if (at >= 0) { /* a card already in the file: the member points at its slot */
            set_member_card(out + ETD_TEAM, m, at);
            continue;
        }
        /* FUN_088ef530: a new card goes to the member's own slot */
        memcpy(out + ETD_CARD + m * sizeof(OkeDataBase), &g_save.db.card[c], sizeof(OkeDataBase));
        set_member_card(out + ETD_TEAM, m, m);
        used[m] = c;
        count++;
        manual += team_trade_manual(&g_save.db.card[c]);
    }
    put_word(out + ETD_COUNT, (uint32_t)count);
    put_word(out + ETD_MANUAL, (uint32_t)manual);
    memcpy(out + ETD_VERSION, SAVE_VERSION, strlen(SAVE_VERSION));
    put_word(out + ETD_SUM, byte_sum(out, ETD_SUM));
    return true;
}

int team_trade_import(const uint8_t file[TEAM_FILE_SIZE])
{
    if (byte_sum(file, ETD_SUM) != get_word(file + ETD_SUM)) {
        SDL_Log("[team] the team file's sum doesn't match");
        return -1;
    }
    /* FUN_08ab7b54: the counts must be the file's, and there must be a card */
    const OkeDataBase *cards[TEAM_MEMBERS];
    int count = 0, manual = 0;
    for (int k = 0; k < TEAM_MEMBERS; k++) {
        cards[k] = (const OkeDataBase *)(file + ETD_CARD + k * sizeof(OkeDataBase));
        count += card_status(cards[k]) != 0;
        manual += team_trade_manual(cards[k]);
    }
    if ((int)get_word(file + ETD_COUNT) != count || (int)get_word(file + ETD_MANUAL) != manual || count <= 0) {
        SDL_Log("[team] the team file's counts don't match (%d/%d cards, %d/%d manual)",
                (int)get_word(file + ETD_COUNT), count, (int)get_word(file + ETD_MANUAL), manual);
        return -1;
    }
    const uint8_t *team = file + ETD_TEAM;
    int local[TEAM_MEMBERS], keep[TEAM_MEMBERS];
    for (int m = 0; m < TEAM_MEMBERS; m++) {
        int k = member_card(team, m);
        local[m] = k >= 0 && k < TEAM_MEMBERS ? k : -1;
        keep[m] = k;
    }
    int t = install(team, cards, local, keep);
    SDL_Log("[team] team file installed as team %d", t + 1);
    return t;
}

/* ---- the manual check ---- */

bool team_trade_manual(const OkeDataBase *card)
{
    SoftwareDataBase *sw = (SoftwareDataBase *)&card->software;
    for (int p = 0; p < PGCHIP_PROGRAM_MAX; p++) {
        PgChipArea a = pgchip_area(sw, p);
        for (int i = 0; i < a.width * a.width; i++) {
            const PgChipCommand *c = &a.chips[i];
            int type = pgchip_type(c);
            if (type == 9 || type == 0x1B || type == 0x1C || (type == 5 && c->b[3] >= 0x26 && c->b[3] <= 0x28))
                return true;
        }
    }
    return false;
}
