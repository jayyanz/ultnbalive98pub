/*
 * All-Star jerseys (0.7.9; Jason, 2026-11-01).
 *
 * In an All-Star game each player wears his club's jersey (the All-Star team packs x1/x2 in
 * TEAMDATA.VIV hold only shoes). The club comes from the All-Star record's ORIGTEAM field
 * (PLAYERS.DBF, records 384 to 410: East team 30, West 31), which EA filled with the player's
 * 1997-98 club. Four players changed teams between the 1997 All-Star game (Cleveland,
 * 9 February 1997) and the 1997-98 season, so they wore the wrong jersey:
 *   406 Shawn Kemp         Cavaliers (4)  -> SuperSonics (24)   traded September 1997
 *   407 Terrell Brandon    Bucks (14)     -> Cavaliers (4)      the same three-team trade
 *   410 Vin Baker          SuperSonics (24) -> Bucks (14)       the same trade
 *   386 Chris Gatling      Nets (16)      -> Mavericks (5)      traded 17 February 1997
 * Every other All-Star record already has his February 1997 club (checked against the NBA's
 * 1997 rosters; Charles Barkley stays a Rocket, his club since August 1996; Chris Webber's
 * Bullets are the Wizards' jersey). Jersey numbers in the All-Star records are left as EA set
 * them (they are the 1997 ones, Kemp 40, Brandon 1).
 *
 * [nba98] AllStarJerseys=1 (default) sets these four ORIGTEAM values in the three roster copies
 * in nba98data (dbdata, origdb, saves\default), the same way Jordan's fields are set; 0 puts
 * EA's values back. Each record is only changed if its PLAYERID and NAME are as expected. The
 * game's own folders are never changed. Needs the roster copy (CutPlayers or Jordan on).
 */

typedef struct { int pid; const char *name; int team_1997; } allstar_fix_t;
static const allstar_fix_t k_allstar_fix[] = {
    {406, "Kemp", 24},
    {407, "Brandon", 4},
    {410, "Baker", 14},
    {386, "Gatling", 5},
};
#define ALLSTAR_NFIX ((int)(sizeof k_allstar_fix / sizeof k_allstar_fix[0]))

static void allstar_setup(void)
{
    enum { F_ID, F_NAME, F_TEAM, F_N };
    static const char *const fname[F_N] = {"PLAYERID", "NAME", "ORIGTEAM"};
    int fo[F_N], fl[F_N], on = ini_int("AllStarJerseys", 1), j, f, ok = 1, changed = 0;
    char orig[MAX_PATH], dst[MAX_PATH], v[16];
    unsigned char *od, *recs = NULL, *mask = NULL;
    DWORD osize = 0;
    uint16_t hl, rl;
    uint32_t nrec, i;
    rm_section s;

    if (!g_roster_active) {
        LOG("All-Star jerseys: roster copy not in use (CutPlayers=0 and Jordan=0); EA's All-Star clubs used");
        return;
    }
    _snprintf(orig, sizeof orig, "%sorigdb\\PLAYERS.DBF", g_exe_dir);
    od = rm_read_all(orig, &osize);
    if (!od) { LOG("All-Star jerseys: cannot read %s; not changed", orig); return; }
    memcpy(&nrec, od + 4, 4); memcpy(&hl, od + 8, 2); memcpy(&rl, od + 10, 2);
    for (f = 0; f < F_N; f++) if (!dbf_field(od, osize, fname[f], &fo[f], &fl[f])) break;
    if (f < F_N || (uint64_t)hl + (uint64_t)nrec * rl > osize) {
        LOG("All-Star jerseys: %s has an unexpected layout; not changed", orig);
        goto done;
    }
    recs = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ALLSTAR_NFIX * rl);
    mask = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, rl);
    if (!recs || !mask) goto done;
    memset(mask + fo[F_TEAM], 1, fl[F_TEAM]);
    memset(&s, 0, sizeof s);
    for (j = 0; j < ALLSTAR_NFIX; j++) {
        const allstar_fix_t *x = &k_allstar_fix[j];
        unsigned char *r = recs + s.count * rl;
        char idbuf[16], name[32];
        int found = 0;
        for (i = 0; i < nrec && !found; i++) {
            const unsigned char *o = od + hl + i * rl;
            _snprintf(idbuf, sizeof idbuf, "%.*s", fl[F_ID], (const char *)o + fo[F_ID]);
            if (atoi(idbuf) == x->pid) { memcpy(r, o, rl); found = 1; }
        }
        if (!found) { LOG("All-Star jerseys: no PLAYERID %d in %s; %s left as it is", x->pid, orig, x->name); ok = 0; continue; }
        _snprintf(name, sizeof name, "%.*s", fl[F_NAME] < 31 ? fl[F_NAME] : 31, (const char *)r + fo[F_NAME]);
        if (_strnicmp(name, x->name, strlen(x->name))) {
            LOG("All-Star jerseys: PLAYERID %d is \"%s\", not %s; left as it is", x->pid, name, x->name);
            ok = 0;
            continue;
        }
        if (on) {   /* else EA's value, as read from origdb */
            _snprintf(v, sizeof v, "%d", x->team_1997);
            put_field(r, fo[F_TEAM], fl[F_TEAM], v);
            changed++;
        }
        s.count++;
    }
    if (!s.count) goto done;
    strcpy(s.table, "PLAYERS");
    s.desc_crc = crc32_buf(od + 32, hl - 32);
    s.reclen = rl;
    s.key_off = (uint16_t)fo[F_ID];
    s.key_len = (uint8_t)fl[F_ID];
    s.op = RM_MODIFY;
    s.mask = mask;
    s.recs = recs;
    s.file = -1;
    {
        static const char *const folders[3] = {"dbdata\\PLAYERS.DBF", "origdb\\PLAYERS.DBF", "saves\\default\\PLAYERS.DBF"};
        for (j = 0; j < 3; j++) {
            _snprintf(dst, sizeof dst, "%s\\%s", g_roster_root, folders[j]);
            if (GetFileAttributesA(dst) == INVALID_FILE_ATTRIBUTES) continue;
            ok = rm_apply(dst, &s) && ok;
        }
    }
    if (on) LOG("All-Star jerseys: 1997 clubs set for %d players (Kemp Sonics, Brandon Cavaliers, Baker Bucks, Gatling Mavericks)%s",
                changed, ok ? "" : " (with errors)");
    else LOG("All-Star jerseys: AllStarJerseys=0; EA's clubs put back%s", ok ? "" : " (with errors)");
done:
    if (recs) HeapFree(GetProcessHeap(), 0, recs);
    if (mask) HeapFree(GetProcessHeap(), 0, mask);
    HeapFree(GetProcessHeap(), 0, od);
}
