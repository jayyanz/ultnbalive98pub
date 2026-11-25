/*
 * jordan_art.inc - optional personal art pack for Michael Jordan (part of nba98core.dll,
 * included from nba98core.cpp after roster_mod.inc). Spec: project doc nba98-jordan-art-handoff.
 *
 * No art or audio ships with this project. If the user has a pack named mj.dat in the
 * game folder (a BIGF/VIV archive), Jordan's records (PLAYERID 303 Chicago, 404 East
 * All-Stars) use what it holds. Two parts, judged separately:
 *
 *   Art ([nba98] JordanArt=1): three parts, each used if mj.dat has it, each optional
 *     PLAYERPKG 0MJORDAN.FSH   (3D face, found inside art\ingame\players\PLRDATA.VIV)
 *     L_PORT    MJORDAN.BMP    (menu headshot, art\frontend\foregrnd\ports; up to nine:
 *                               MJORDAN.BMP, MJORDAN2.BMP .. MJORDAN9.BMP, one picked per start)
 *     ACTSHOT   MJACT.BMP      (action shot, art\frontend\foregrnd\action; up to nine the same way)
 *   Speech ([nba98] JordanSpeech=1): entries GAMESPCH\MIJORDA.HDR/.DAT (arena announcer)
 *   and PLAYSPCH\MIJORDA.HDR/.DAT (play-by-play), all four or none
 *     SPCHHDR   MIJORDA.HDR, SPCHDATA MIJORDA.DAT
 *
 * A part that is missing, damaged or switched off goes back to the values without the
 * pack: PLAYERPKG and SPCHHDR/SPCHDATA from nba98_jordan.dat (the Create Player face code,
 * REJORDA speech), L_PORT and ACTSHOT from the user's own origdb (1ROPLAYE.FSH, "No photo
 * available"). This runs at every start, in all three roster copies in nba98data, so
 * switching the pack on or off always takes effect. Bad speech never turns the art off.
 *
 * The game's files are never changed. It reads redirected copies:
 *   nba98data\art\MJORDAN.BMP, MJACT.BMP   extracted from mj.dat (MJACT.BMP is one of the
 *                                          pack's action shots, picked at each start)
 *   nba98data\art\PLRDATA.VIV              the game's PLRDATA.VIV + 0MJORDAN.FSH, in EA's
 *                                          sorted order
 *   nba98data\speech\gamespch\PLYRHDRS.VIV, PLYRDATA.VIV, and the same in playspch:
 *                                          the game's banks + mijorda.hdr / mijorda.dat
 *                                          appended at the end (EA's speech banks are not
 *                                          sorted; the game's lookup walks the whole
 *                                          directory)
 * Every original entry is copied byte for byte. A copy is built on the game's first read
 * and rebuilt when the original (size, time) or mj.dat (CRC) changes; a stamp file
 * <copy>.txt holds that. The CreateFileA hook sends the game's opens there, only for the
 * parts in use.
 */

#define ART_NAME_FSH "0MJORDAN.FSH"
#define ART_NAME_PORT "MJORDAN.BMP"
#define ART_NAME_ACT "MJACT.BMP"
#define SPCH_KEY_HDR "MIJORDA.HDR"
#define SPCH_KEY_DAT "MIJORDA.DAT"

static int g_art_on, g_spch_on;            /* parts in use this session (g_art_on: any of the three below) */
static int g_art_face, g_art_port, g_art_act; /* face package, headshot, action shot in use */
static int g_art_pic_ok[2][10];             /* usable headshots [0] / action shots [1] by number (for our menu) */
static const unsigned char *g_art_port_bmp; /* the headshot picked this start (inside mj.dat) */
static unsigned char *g_art_face_buf;       /* 0MJORDAN.FSH with its in-match portrait made from that headshot */
static DWORD g_art_face_n;
static char g_art_dir[MAX_PATH];           /* <install>\nba98data\art */
static char g_spch_dir[MAX_PATH];          /* <install>\nba98data\speech */
static unsigned char *g_art_pack;          /* mj.dat contents */
static DWORD g_art_pack_size;
static uint32_t g_art_pack_crc;
static CRITICAL_SECTION g_art_cs;

static uint32_t be32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put_be32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

typedef struct { const char *name; uint32_t off, size; } viv_entry;

/* parse a BIGF directory; returns the entry count, or -1 if damaged. names point into d. */
static int viv_parse(const unsigned char *d, DWORD size, viv_entry *out, int max)
{
    uint32_t n, hdr, i, p = 16;
    if (size < 16 || memcmp(d, "BIGF", 4)) return -1;
    n = be32(d + 8);
    hdr = be32(d + 12);
    if (hdr > size || n > 100000) return -1;
    for (i = 0; i < n; i++) {
        uint32_t off, sz;
        const unsigned char *e;
        if (p + 9 > hdr) return -1;
        off = be32(d + p); sz = be32(d + p + 4);
        if (off > size || sz > size - off) return -1;
        e = (const unsigned char *)memchr(d + p + 8, 0, hdr - (p + 8));
        if (!e) return -1;
        if ((int)i < max) { out[i].name = (const char *)d + p + 8; out[i].off = off; out[i].size = sz; }
        p = (uint32_t)(e - d) + 1;
    }
    return (int)n;
}

/* an entry of mj.dat by name */
static int pack_entry(const char *name, const unsigned char **data, DWORD *size)
{
    viv_entry e[32];
    int n = viv_parse(g_art_pack, g_art_pack_size, e, 32), i;
    for (i = 0; i < n && i < 32; i++)
        if (!_stricmp(e[i].name, name)) { *data = g_art_pack + e[i].off; *size = e[i].size; return 1; }
    return 0;
}

/* a 32-bit uncompressed BMP of the expected size */
static int art_bmp_ok(const unsigned char *b, uint32_t n, int w, int h, const char *what)
{
    int32_t bw, bh;
    uint16_t bpp;
    uint32_t comp, dataoff;
    if (n < 54 || b[0] != 'B' || b[1] != 'M') { LOG("Jordan art: %s is not a BMP", what); return 0; }
    memcpy(&dataoff, b + 10, 4); memcpy(&bw, b + 18, 4); memcpy(&bh, b + 22, 4); memcpy(&bpp, b + 28, 2); memcpy(&comp, b + 30, 4);
    if (bpp != 32 || comp != 0) { LOG("Jordan art: %s is %u-bit (compression %lu), expected 32-bit uncompressed", what, bpp, (unsigned long)comp); return 0; }
    if (bw != w || (bh != h && bh != -h)) { LOG("Jordan art: %s is %ldx%ld, expected %dx%d", what, (long)bw, (long)bh, w, h); return 0; }
    if ((uint64_t)dataoff + (uint64_t)w * h * 4 > n) { LOG("Jordan art: %s is cut short", what); return 0; }
    return 1;
}

/* load mj.dat; 1 = a complete BIGF archive (its parts are checked separately) */
static int art_load_pack(const char *path)
{
    DWORD size = 0;
    unsigned char *d = rm_read_all(path, &size);
    if (!d) { LOG("Jordan art: cannot read %s", path); return 0; }
    if (viv_parse(d, size, NULL, 0) < 0) {
        LOG("Jordan art: %s is damaged (not a complete BIGF archive); default art and speech used", path);
        HeapFree(GetProcessHeap(), 0, d);
        return 0;
    }
    g_art_pack = d;
    g_art_pack_size = size;
    g_art_pack_crc = crc32_buf(d, size);
    return 1;
}

/* the 3D face package: present and an FSH (SHPI) package */
static int art_face_ok(void)
{
    const unsigned char *f;
    DWORD fn;
    if (!pack_entry(ART_NAME_FSH, &f, &fn)) { LOG("Jordan art: mj.dat has no %s; default face", ART_NAME_FSH); return 0; }
    if (fn < 16 || memcmp(f, "SHPI", 4)) { LOG("Jordan art: %s in mj.dat is not an FSH package; default face", ART_NAME_FSH); return 0; }
    return 1;
}

/* a speech .hdr/.dat pair: hdr byte 3 = take count, byte 4 = .dat length / 256, then per take
   a big-endian offset (units of 256) in 3-byte (arena) or 4-byte (play-by-play) records from
   byte 8; every take starts "PT\0\2" */
static int spch_pair_ok(const unsigned char *h, DWORD hn, const unsigned char *d, DWORD dn)
{
    int takes, rec, t;
    if (hn < 8) return 0;
    takes = h[3];
    if (takes < 1 || takes > 32 || (DWORD)h[4] * 256 != dn) return 0;
    for (rec = 3; rec <= 4; rec++) {
        int good = 8 + takes * rec <= (int)hn;
        for (t = 0; good && t < takes; t++) {
            DWORD off = (((DWORD)h[8 + t * rec] << 8) | h[9 + t * rec]) * 256;
            if (off + 4 > dn || memcmp(d + off, "PT\0\2", 4)) good = 0;
        }
        if (good) return 1;
    }
    return 0;
}

static int spch_part_ok(void)
{
    static const char *const banks[2] = {"GAMESPCH", "PLAYSPCH"};
    int b, any = 0, all = 1;
    for (b = 0; b < 2; b++) {
        char hn[64], dn[64];
        const unsigned char *h, *d;
        DWORD hs, ds;
        int hh, hd;
        _snprintf(hn, sizeof hn, "%s\\%s", banks[b], SPCH_KEY_HDR);
        _snprintf(dn, sizeof dn, "%s\\%s", banks[b], SPCH_KEY_DAT);
        hh = pack_entry(hn, &h, &hs);
        hd = pack_entry(dn, &d, &ds);
        any |= hh | hd;
        if (!hh || !hd) { all = 0; continue; }
        if (!spch_pair_ok(h, hs, d, ds)) {
            LOG("Jordan speech: %s / %s in mj.dat are damaged; REJORDA speech used", hn, dn);
            return 0;
        }
    }
    if (!any) { LOG("Jordan speech: mj.dat has no speech entries; REJORDA speech used"); return 0; }
    if (!all) { LOG("Jordan speech: mj.dat has only some of the four speech entries; REJORDA speech used"); return 0; }
    return 1;
}

/* write a file only if its contents differ */
static int art_write_if_changed(const char *path, const unsigned char *data, DWORD n)
{
    DWORD old_n = 0;
    unsigned char *old = rm_read_all(path, &old_n);
    int same = old && old_n == n && !memcmp(old, data, n);
    if (old) HeapFree(GetProcessHeap(), 0, old);
    if (same) return 1;
    return rm_write_all(path, data, n, NULL, 0, NULL, 0);
}

/*
 * The headshot and the action shot: mj.dat may hold up to nine of each, all optional.
 *   headshot     MJORDAN.BMP, MJORDAN2.BMP .. MJORDAN9.BMP  103x118   [nba98] JordanHeadshot
 *   action shot  MJACT.BMP,   MJACT2.BMP   .. MJACT9.BMP    146x205   [nba98] JordanActionShot
 * Each is checked (BMP, 32-bit uncompressed, that size); a bad one is logged and left out.
 * The game only asks for MJORDAN.BMP / MJACT.BMP, so at each start one usable picture of each
 * kind is written under that name to nba98data\art\, the file the redirect serves.
 * Setting "random" (default): a random pick, never the one shown at the previous start
 * (kept in nba98data\art\mjordan.txt / mjact.txt). 1 to 9 pins one (1 = the plain name); if
 * that one is not usable, the first usable one is used (logged).
 * No usable picture of a kind: that picture stays the default ("No photo available"); the
 * face and the other picture are not affected.
 */
typedef struct { const char *stem, *what, *ini, *last; int w, h, kind; } art_pic;
static const art_pic k_pic_port = {"MJORDAN", "headshot", "JordanHeadshot", "mjordan.txt", 103, 118, 0};
static const art_pic k_pic_act = {"MJACT", "action shot", "JordanActionShot", "mjact.txt", 146, 205, 1};

static void art_pic_name(const art_pic *pc, int k, char *out, size_t cap)
{
    if (k == 1) _snprintf(out, cap, "%s.BMP", pc->stem); else _snprintf(out, cap, "%s%d.BMP", pc->stem, k);
    out[cap - 1] = 0;
}

static int art_extract_pic(const art_pic *pc)
{
    const unsigned char *d[10], *b;
    DWORD n[10], bn;
    int ok[10], count = 0, pick = 0, k, pin, pinned = 0;
    char name[24], want[16], path[MAX_PATH];
    memset(ok, 0, sizeof ok);
    for (k = 1; k <= 9; k++) {
        art_pic_name(pc, k, name, sizeof name);
        if (!pack_entry(name, &b, &bn)) continue;
        if (!art_bmp_ok(b, bn, pc->w, pc->h, name)) { LOG("Jordan art: %s left out of the %ss", name, pc->what); continue; }
        d[k] = b; n[k] = bn; ok[k] = 1; count++;
        g_art_pic_ok[pc->kind][k] = 1;
        if (!pick) pick = k;   /* the first usable one */
    }
    if (!count) { LOG("Jordan art: no usable %s in mj.dat; default %s (no photo)", pc->what, pc->what); return 0; }
    GetPrivateProfileStringA("nba98", pc->ini, "random", want, sizeof want, g_ini);
    pin = atoi(want);
    if (pin >= 1 && pin <= 9) {
        pinned = 1;
        if (ok[pin]) pick = pin;
        else {
            art_pic_name(pc, pick, name, sizeof name);
            LOG("Jordan art: %s=%d but mj.dat has no usable %s %d; %s used", pc->ini, pin, pc->what, pin, name);
        }
    } else if (count > 1) {
        /* random, but never the one shown last time */
        struct { LARGE_INTEGER t; DWORD tick, pid; SYSTEMTIME st; } seed;
        char last_path[MAX_PATH];
        DWORD ln = 0;
        unsigned char *lt;
        int last = 0, i = 0, choices = count;
        uint32_t r;
        _snprintf(last_path, sizeof last_path, "%s\\%s", g_art_dir, pc->last);
        lt = rm_read_all(last_path, &ln);
        if (lt) { lt[ln] = 0; last = atoi((char *)lt); HeapFree(GetProcessHeap(), 0, lt); }
        if (last >= 1 && last <= 9 && ok[last]) choices--; else last = 0;
        QueryPerformanceCounter(&seed.t);
        seed.tick = GetTickCount();
        seed.pid = GetCurrentProcessId();
        GetSystemTime(&seed.st);
        /* the kind is mixed in so the headshot and action shot picks, made in the same instant, differ */
        r = (crc32_buf((const unsigned char *)&seed, sizeof seed) ^ crc32_buf((const unsigned char *)pc->stem, strlen(pc->stem)) *
             2654435761u) % (uint32_t)choices;
        for (k = 1; k <= 9; k++)
            if (ok[k] && k != last && (uint32_t)i++ == r) { pick = k; break; }
        _snprintf(want, sizeof want, "%d\r\n", pick);
        rm_write_all(last_path, want, (DWORD)strlen(want), NULL, 0, NULL, 0);
    }
    art_pic_name(pc, 1, name, sizeof name);   /* the name the game asks for */
    _snprintf(path, sizeof path, "%s\\%s", g_art_dir, name);
    if (!art_write_if_changed(path, d[pick], n[pick])) {
        LOG("Jordan art: could not write %s (error %lu); default %s", path, GetLastError(), pc->what);
        return 0;
    }
    if (pc->kind == 0) g_art_port_bmp = d[pick];
    art_pic_name(pc, pick, name, sizeof name);
    LOG("Jordan art: %s %s (%s, %d in mj.dat)", pc->what, name,
        pinned ? "pinned" : count > 1 ? "random, not the last one shown" : "the only one", count);
    return 1;
}

/* ---------------------------------------------------------------- roster fields */

/* field offset and length in a dBase III record, from the header */
static int dbf_field(const unsigned char *d, DWORD size, const char *name, int *off, int *len)
{
    uint16_t hl;
    DWORD p = 32;
    int o = 1;
    if (size < 32) return 0;
    memcpy(&hl, d + 8, 2);
    while (p + 32 <= hl && p + 32 <= size && d[p] != 0x0D) {
        char nm[12];
        memcpy(nm, d + p, 11); nm[11] = 0;
        if (!_stricmp(nm, name)) { *off = o; *len = d[p + 16]; return 1; }
        o += d[p + 16];
        p += 32;
    }
    return 0;
}

static void put_field(unsigned char *r, int off, int len, const char *v)
{
    memset(r + off, ' ', len);
    memcpy(r + off, v, strlen(v));
}

/* Put Jordan's art and speech fields into the roster copies: the pack's names for the
   parts in use, the values without the pack for the others. Idempotent: unchanged tables
   are not rewritten. */
static void art_roster(int face, int port, int act, int spch)
{
    static const int pids[2] = {303, 404};
    enum { F_ID, F_PKG, F_PORT, F_ACT, F_SHDR, F_SDAT, F_N };
    static const char *const fname[F_N] = {"PLAYERID", "PLAYERPKG", "L_PORT", "ACTSHOT", "SPCHHDR", "SPCHDATA"};
    int fo[F_N], fl[F_N];
    char orig[MAX_PATH], dst[MAX_PATH];
    unsigned char *od, *recs = NULL, *mask = NULL;
    DWORD osize = 0;
    uint16_t hl, rl;
    uint32_t nrec, i;
    int j, f, ok = 1;
    const rm_section *jsec = NULL;
    rm_section s;

    _snprintf(orig, sizeof orig, "%sorigdb\\PLAYERS.DBF", g_exe_dir);
    od = rm_read_all(orig, &osize);
    if (!od) { LOG("Jordan art: cannot read %s; Jordan's art fields not checked", orig); return; }
    memcpy(&nrec, od + 4, 4); memcpy(&hl, od + 8, 2); memcpy(&rl, od + 10, 2);
    for (f = 0; f < F_N; f++)
        if (!dbf_field(od, osize, fname[f], &fo[f], &fl[f]) || (f > F_ID && fl[f] < 12 && f != F_SHDR && f != F_SDAT) ||
            ((f == F_SHDR || f == F_SDAT) && fl[f] < 11))
            break;
    if (f < F_N || (uint64_t)hl + (uint64_t)nrec * rl > osize) {
        LOG("Jordan art: %s has an unexpected layout; Jordan's art fields not changed", orig);
        goto done;
    }
    /* nba98_jordan.dat's PLAYERS section: the face code and speech keys without the pack */
    for (j = 0; j < g_rm_nsec; j++)
        if (!_stricmp(g_rm_files[g_rm_sec[j].file].name, "nba98_jordan.dat") && !_stricmp(g_rm_sec[j].table, "PLAYERS") &&
            g_rm_sec[j].reclen == rl)
            jsec = &g_rm_sec[j];

    recs = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 2 * rl);
    mask = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, rl);
    if (!recs || !mask) goto done;
    for (f = F_PKG; f < F_N; f++) memset(mask + fo[f], 1, fl[f]);
    memset(&s, 0, sizeof s);
    for (j = 0; j < 2; j++) {
        unsigned char *r = recs + s.count * rl;
        const unsigned char *jr = NULL;
        int found = 0;
        for (i = 0; i < nrec; i++) {
            const unsigned char *o = od + hl + i * rl;
            char idbuf[16];
            _snprintf(idbuf, sizeof idbuf, "%.*s", fl[F_ID], (const char *)o + fo[F_ID]);
            if (atoi(idbuf) == pids[j]) { memcpy(r, o, rl); found = 1; break; }
        }
        if (!found) { LOG("Jordan art: no PLAYERID %d in %s", pids[j], orig); ok = 0; continue; }
        if (jsec) {
            uint32_t k;
            for (k = 0; k < jsec->count; k++)
                if (!memcmp(jsec->recs + k * rl + fo[F_ID], r + fo[F_ID], fl[F_ID])) jr = jsec->recs + k * rl;
        }
        /* defaults: face code and speech from nba98_jordan.dat, pictures as the user's origdb */
        if (jr) {
            memcpy(r + fo[F_PKG], jr + fo[F_PKG], fl[F_PKG]);
            memcpy(r + fo[F_SHDR], jr + fo[F_SHDR], fl[F_SHDR]);
            memcpy(r + fo[F_SDAT], jr + fo[F_SDAT], fl[F_SDAT]);
        }
        if (face) put_field(r, fo[F_PKG], fl[F_PKG], ART_NAME_FSH);
        if (port) put_field(r, fo[F_PORT], fl[F_PORT], ART_NAME_PORT);
        if (act) put_field(r, fo[F_ACT], fl[F_ACT], ART_NAME_ACT);
        if (spch) {
            put_field(r, fo[F_SHDR], fl[F_SHDR], SPCH_KEY_HDR);
            put_field(r, fo[F_SDAT], fl[F_SDAT], SPCH_KEY_DAT);
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
    LOG("Jordan art: roster fields set: face %s, headshot %s, action shot %s, speech %s%s",
        face ? "0MJORDAN.FSH" : "default (face code)", port ? "MJORDAN.BMP" : "default (no photo)",
        act ? "MJACT.BMP" : "default (no photo)", spch ? "MIJORDA (pack)" : "REJORDA (default)", ok ? "" : " (with errors)");
done:
    if (recs) HeapFree(GetProcessHeap(), 0, recs);
    if (mask) HeapFree(GetProcessHeap(), 0, mask);
    HeapFree(GetProcessHeap(), 0, od);
}

/* ---------------------------------------------------------------- archive copies */

/* EA's PLRDATA.VIV is sorted by name compared in upper case (so '_' sorts after letters) */
static int art_name_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = toupper((unsigned char)*a), cb = toupper((unsigned char)*b);
        if (ca != cb || !ca) return ca - cb;
    }
}

/* Build dst from the game's archive src with one entry added (or replaced), unless the
   stamp (dst + ".txt") says it is current. sorted: insert in EA's sorted order; otherwise
   append at the end. Written as EA writes BIGF: big-endian sizes, the header-size field and
   every entry 4-byte aligned. */
static int art_build_viv(const char *src, const char *dst, const char *add_name, const unsigned char *add, DWORD add_size, int sorted)
{
    char stamp_path[MAX_PATH], stamp[256];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    unsigned char *d = NULL, *out = NULL, *old = NULL;
    DWORD size = 0, oldn = 0;
    viv_entry *e = NULL;
    int n, i, ins, ok = 0, replaced = -1;
    uint32_t hdr, off, total;

    _snprintf(stamp_path, sizeof stamp_path, "%s.txt", dst);
    if (!GetFileAttributesExA(src, GetFileExInfoStandard, &fa)) { LOG("Jordan art: cannot find %s", src); return 0; }
    _snprintf(stamp, sizeof stamp, "source size %lu time %08lx%08lx, mj.dat crc32 %08lx, entry crc32 %08lx\r\n",
              (unsigned long)fa.nFileSizeLow, (unsigned long)fa.ftLastWriteTime.dwHighDateTime,
              (unsigned long)fa.ftLastWriteTime.dwLowDateTime, (unsigned long)g_art_pack_crc, (unsigned long)crc32_buf(add, add_size));
    old = rm_read_all(stamp_path, &oldn);
    if (old && oldn == strlen(stamp) && !memcmp(old, stamp, oldn) && GetFileAttributesA(dst) != INVALID_FILE_ATTRIBUTES) {
        HeapFree(GetProcessHeap(), 0, old);
        return 1;
    }
    if (old) HeapFree(GetProcessHeap(), 0, old);

    d = rm_read_all(src, &size);
    if (!d) { LOG("Jordan art: cannot read %s", src); return 0; }
    n = viv_parse(d, size, NULL, 0);
    if (n < 0) { LOG("Jordan art: %s is not a BIGF archive; %s not added", src, add_name); goto done; }
    e = (viv_entry *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof *e);
    if (!e) goto done;
    viv_parse(d, size, e, n);
    for (i = 0; i < n; i++) if (!_stricmp(e[i].name, add_name)) replaced = i;
    if (replaced >= 0) {
        e[replaced].off = 0xFFFFFFFFu;   /* marks "use the pack's data" */
        e[replaced].size = add_size;
    } else {
        ins = n;
        if (sorted) for (ins = 0; ins < n && art_name_cmp(e[ins].name, add_name) < 0; ins++) {}
        memmove(e + ins + 1, e + ins, (n - ins) * sizeof *e);
        e[ins].name = add_name;
        e[ins].off = 0xFFFFFFFFu;
        e[ins].size = add_size;
        n++;
    }
    hdr = 16;
    for (i = 0; i < n; i++) hdr += 8 + (uint32_t)strlen(e[i].name) + 1;
    off = (hdr + 3) & ~3u;
    total = off;
    for (i = 0; i < n; i++) total += (e[i].size + 3) & ~3u;
    out = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (!out) goto done;
    memcpy(out, "BIGF", 4);
    put_be32(out + 4, total);
    put_be32(out + 8, (uint32_t)n);
    put_be32(out + 12, off);             /* EA stores the aligned directory size */
    {
        uint32_t p = 16, cur = off;
        for (i = 0; i < n; i++) {
            size_t ln = strlen(e[i].name) + 1;
            put_be32(out + p, cur);
            put_be32(out + p + 4, e[i].size);
            memcpy(out + p + 8, e[i].name, ln);
            p += 8 + (uint32_t)ln;
            memcpy(out + cur, e[i].off == 0xFFFFFFFFu ? add : d + e[i].off, e[i].size);
            cur += (e[i].size + 3) & ~3u;
        }
    }
    if (!rm_write_all(dst, out, total, NULL, 0, NULL, 0)) { LOG("Jordan art: could not write %s (error %lu)", dst, GetLastError()); goto done; }
    if (!rm_write_all(stamp_path, stamp, (DWORD)strlen(stamp), NULL, 0, NULL, 0)) LOG("Jordan art: could not write %s", stamp_path);
    LOG("Jordan art: built %s from %s (%d entries, %s %s)", dst, src, n, replaced >= 0 ? "replaced" : "added", add_name);
    ok = 1;
done:
    if (out) HeapFree(GetProcessHeap(), 0, out);
    if (e) HeapFree(GetProcessHeap(), 0, e);
    if (d) HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

/* ---------------------------------------------------------------- in-match portrait */
/*
 * In a match (foul and free throw panels) the game does not use the headshot (L_PORT) but the
 * "port" picture inside the player's 3D face package: 85x96, 8-bit (type 0x7B) with a 256-entry
 * 32-bit palette (type 0x2A, BGRA) where index 255 is see-through. Jordan's comes from
 * 0MJORDAN.FSH in mj.dat, so it ignored the headshot picked for the menus (Jason, 2026-11-01).
 * We build a copy of the face package with "port" made from the picked headshot: scaled to
 * 85x96 by area averaging (the headshots are premultiplied), alpha below half see-through,
 * 255 colours by median cut. The copy goes into nba98data\art\PLRDATA.VIV instead of the pack's.
 */
#define PORT_W 85
#define PORT_H 96

typedef struct { unsigned char c[3]; } port_px;

static int port_cmp_ch;
static int port_cmp(const void *a, const void *b)
{
    return (int)((const port_px *)a)->c[port_cmp_ch] - (int)((const port_px *)b)->c[port_cmp_ch];
}

/* headshot BMP (32-bit, premultiplied) -> 85x96 indices and a BGRA palette */
static void art_port_image(const unsigned char *bmp, unsigned char *pix, unsigned char *pal)
{
    int32_t bw, bh;
    uint32_t off;
    int x, y, i, n = 0, nbox, ch;
    static port_px px[PORT_W * PORT_H];
    static float col[PORT_W * PORT_H][4];
    struct { int lo, hi; } box[255];
    memcpy(&off, bmp + 10, 4); memcpy(&bw, bmp + 18, 4); memcpy(&bh, bmp + 22, 4);
    {
        int h = bh < 0 ? -bh : bh;
        for (y = 0; y < PORT_H; y++)
            for (x = 0; x < PORT_W; x++) {
                /* area of the source covered by this output pixel */
                double x0 = (double)x * bw / PORT_W, x1 = (double)(x + 1) * bw / PORT_W;
                double y0 = (double)y * h / PORT_H, y1 = (double)(y + 1) * h / PORT_H, acc[4] = {0, 0, 0, 0}, wsum = 0;
                int sx, sy;
                for (sy = (int)y0; sy < h && sy < y1; sy++) {
                    double wy = (sy + 1 < y1 ? sy + 1 : y1) - (sy > y0 ? sy : y0);
                    int row = bh < 0 ? sy : h - 1 - sy;   /* positive height = stored bottom-up */
                    for (sx = (int)x0; sx < bw && sx < x1; sx++) {
                        double wx = (sx + 1 < x1 ? sx + 1 : x1) - (sx > x0 ? sx : x0), w = wx * wy;
                        const unsigned char *p = bmp + off + ((size_t)row * bw + sx) * 4;
                        acc[0] += w * p[0]; acc[1] += w * p[1]; acc[2] += w * p[2]; acc[3] += w * p[3];
                        wsum += w;
                    }
                }
                for (i = 0; i < 4; i++) col[y * PORT_W + x][i] = wsum > 0 ? (float)(acc[i] / wsum) : 0;
            }
    }
    /* the opaque pixels, un-premultiplied */
    for (i = 0; i < PORT_W * PORT_H; i++) {
        float a = col[i][3];
        if (a < 128) continue;
        for (ch = 0; ch < 3; ch++) {
            float v = col[i][ch] * 255.0f / a;
            px[n].c[ch] = (unsigned char)(v > 255 ? 255 : v + 0.5f);
        }
        n++;
    }
    /* median cut into at most 255 boxes */
    nbox = 0;
    if (n) { box[0].lo = 0; box[0].hi = n; nbox = 1; }
    while (nbox < 255) {
        int best = -1, bestr = 0, bestch = 0;
        for (i = 0; i < nbox; i++) {
            int mn[3] = {255, 255, 255}, mx[3] = {0, 0, 0}, k;
            if (box[i].hi - box[i].lo < 2) continue;
            for (k = box[i].lo; k < box[i].hi; k++)
                for (ch = 0; ch < 3; ch++) {
                    if (px[k].c[ch] < mn[ch]) mn[ch] = px[k].c[ch];
                    if (px[k].c[ch] > mx[ch]) mx[ch] = px[k].c[ch];
                }
            for (ch = 0; ch < 3; ch++)
                if (mx[ch] - mn[ch] > bestr) { bestr = mx[ch] - mn[ch]; best = i; bestch = ch; }
        }
        if (best < 0) break;
        port_cmp_ch = bestch;
        qsort(px + box[best].lo, box[best].hi - box[best].lo, sizeof *px, port_cmp);
        box[nbox].hi = box[best].hi;
        box[best].hi = box[nbox].lo = (box[best].lo + box[best].hi) / 2;
        nbox++;
    }
    memset(pal, 0, 1024);
    for (i = 0; i < nbox; i++) {
        double s[3] = {0, 0, 0};
        int k, cnt = box[i].hi - box[i].lo;
        for (k = box[i].lo; k < box[i].hi; k++) for (ch = 0; ch < 3; ch++) s[ch] += px[k].c[ch];
        for (ch = 0; ch < 3; ch++) pal[i * 4 + ch] = (unsigned char)(s[ch] / cnt + 0.5);
        pal[i * 4 + 3] = 255;
    }
    /* each pixel to the nearest palette colour; see-through ones to 255 */
    for (i = 0; i < PORT_W * PORT_H; i++) {
        float a = col[i][3], c[3];
        int k, bk = 255;
        long bd = 0x7FFFFFFF;
        if (a < 128 || !nbox) { pix[i] = 255; continue; }
        for (ch = 0; ch < 3; ch++) { c[ch] = col[i][ch] * 255.0f / a; if (c[ch] > 255) c[ch] = 255; }
        for (k = 0; k < nbox; k++) {
            long d0 = (long)c[0] - pal[k * 4], d1 = (long)c[1] - pal[k * 4 + 1], d2 = (long)c[2] - pal[k * 4 + 2];
            long dd = d0 * d0 + d1 * d1 + d2 * d2;
            if (dd < bd) { bd = dd; bk = k; }
        }
        pix[i] = (unsigned char)bk;
    }
}

/* 0MJORDAN.FSH from mj.dat with its "port" picture replaced (or added) */
static void art_face_portrait(void)
{
    const unsigned char *f;
    DWORD fn, total;
    uint32_t n, i, j, *off = NULL, *len = NULL, port_len = 16 + PORT_W * PORT_H + 16 + 1024;
    int port_at = -1;
    unsigned char *o, *q;
    if (!g_art_port_bmp || !pack_entry(ART_NAME_FSH, &f, &fn) || fn < 16) return;
    memcpy(&n, f + 8, 4);
    if (n > 64 || 16 + 8 * n > fn) { LOG("Jordan art: %s has an unexpected directory; in-match portrait left as it is", ART_NAME_FSH); return; }
    off = (uint32_t *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * 8);
    if (!off) return;
    len = off + n + 1;
    for (i = 0; i < n; i++) {
        memcpy(&off[i], f + 16 + 8 * i + 4, 4);
        if (!memcmp(f + 16 + 8 * i, "port", 4)) port_at = (int)i;
    }
    /* each entry runs to the next higher offset (or the end) */
    for (i = 0; i < n; i++) {
        uint32_t end = fn;
        for (j = 0; j < n; j++) if (off[j] > off[i] && off[j] < end) end = off[j];
        if (off[i] > fn) { LOG("Jordan art: %s is damaged; in-match portrait left as it is", ART_NAME_FSH); goto done; }
        len[i] = end - off[i];
    }
    total = 16 + 8 * (n + (port_at < 0));
    for (i = 0; i < n; i++) total += (int)i == port_at ? port_len : len[i];
    if (port_at < 0) total += port_len;
    o = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, total);
    if (!o) goto done;
    memcpy(o, "SHPI", 4); memcpy(o + 4, &total, 4);
    j = n + (port_at < 0); memcpy(o + 8, &j, 4);
    memcpy(o + 12, f + 12, 4);   /* "GIMX" */
    q = o + 16 + 8 * j;
    for (i = 0; i <= n; i++) {
        uint32_t at = (uint32_t)(q - o);
        int ours = (int)i == port_at || (i == n && port_at < 0);
        if (i == n && !ours) break;
        memcpy(o + 16 + 8 * i, ours ? (const unsigned char *)"port" : f + 16 + 8 * i, 4);
        memcpy(o + 16 + 8 * i + 4, &at, 4);
        if (!ours) { memcpy(q, f + off[i], len[i]); q += len[i]; continue; }
        {
            uint32_t next = 16 + PORT_W * PORT_H;
            uint16_t hdr[6] = {PORT_W, PORT_H, PORT_W / 2, PORT_H / 2, 0, 0}, phdr[6] = {256, 1, 256, 0, 0, 0};
            q[0] = 0x7B; q[1] = (unsigned char)next; q[2] = (unsigned char)(next >> 8); q[3] = (unsigned char)(next >> 16);
            memcpy(q + 4, hdr, 12);
            art_port_image(g_art_port_bmp, q + 16, q + next + 16);
            q += next;
            q[0] = 0x2A;
            memcpy(q + 4, phdr, 12);
            q += 16 + 1024;
        }
    }
    g_art_face_buf = o;
    g_art_face_n = total;
    LOG("Jordan art: in-match portrait (fouls, free throws) made from the headshot in use (%s %s)",
        port_at < 0 ? "added to" : "replaces the one in", ART_NAME_FSH);
done:
    HeapFree(GetProcessHeap(), 0, off);
}

/* ---------------------------------------------------------------- redirection */

/* the archives we hand the game copies of */
typedef struct {
    const char *folder;      /* parent folder name to match, or NULL for any */
    const char *base;        /* file name */
    int part;                /* 0 art, 1 speech */
    const char *entry;       /* mj.dat entry to add */
    const char *add_name;    /* its name in the copy */
    int sorted;
    char dst[MAX_PATH];
    char src[MAX_PATH];
    int state;               /* 0 not tried, 1 ready, -1 failed */
} art_viv;

static art_viv g_art_vivs[] = {
    {NULL, "PLRDATA.VIV", 0, ART_NAME_FSH, ART_NAME_FSH, 1},
    {"gamespch", "PLYRHDRS.VIV", 1, "GAMESPCH\\" SPCH_KEY_HDR, "mijorda.hdr", 0},
    {"gamespch", "PLYRDATA.VIV", 1, "GAMESPCH\\" SPCH_KEY_DAT, "mijorda.dat", 0},
    {"playspch", "PLYRHDRS.VIV", 1, "PLAYSPCH\\" SPCH_KEY_HDR, "mijorda.hdr", 0},
    {"playspch", "PLYRDATA.VIV", 1, "PLAYSPCH\\" SPCH_KEY_DAT, "mijorda.dat", 0},
};
#define ART_NVIV ((int)(sizeof g_art_vivs / sizeof g_art_vivs[0]))

static const char *art_base(const char *p)
{
    const char *b1 = strrchr(p, '\\'), *b2 = strrchr(p, '/'), *b = p;
    if (b1 && b1 + 1 > b) b = b1 + 1;
    if (b2 && b2 + 1 > b) b = b2 + 1;
    return b;
}

/* name of the folder holding a full path ("...\gamespch\PLYRDATA.VIV" -> "gamespch") */
static void art_parent(const char *full, char *out, size_t cap)
{
    const char *b = art_base(full);
    const char *e = b > full ? b - 1 : full, *s = e;
    while (s > full && s[-1] != '\\' && s[-1] != '/') s--;
    _snprintf(out, cap, "%.*s", (int)(e - s), s);
    out[cap - 1] = 0;
}

/* path to open instead of name (into out), or 0 to open name as asked */
static int art_redirect(const char *name, DWORD access, char *out, size_t cap)
{
    const char *b;
    int v;
    if ((!g_art_on && !g_spch_on) || !name) return 0;
    b = art_base(name);
    /* the pictures are also opened read/write once (an existence test); that open goes to
       our copy too. The archives are only ever read. */
    if ((g_art_port && !_stricmp(b, ART_NAME_PORT)) || (g_art_act && !_stricmp(b, ART_NAME_ACT))) {
        _snprintf(out, cap, "%s\\%s", g_art_dir, b);
        return 1;
    }
    if (access & (GENERIC_WRITE | GENERIC_ALL)) return 0;
    for (v = 0; v < ART_NVIV; v++) {
        art_viv *a = &g_art_vivs[v];
        char full[MAX_PATH], parent[64];
        const unsigned char *add;
        DWORD add_size;
        int ready;
        if (_stricmp(b, a->base) || !(a->part ? g_spch_on : g_art_face)) continue;
        if (!GetFullPathNameA(name, sizeof full, full, NULL)) return 0;
        if (!_strnicmp(full, g_roster_root, strlen(g_roster_root))) return 0;   /* already our copy */
        if (a->folder) {
            art_parent(full, parent, sizeof parent);
            if (_stricmp(parent, a->folder)) continue;
        }
        if (!pack_entry(a->entry, &add, &add_size)) return 0;
        if (!a->part && g_art_face_buf) { add = g_art_face_buf; add_size = g_art_face_n; }   /* face with our portrait */
        EnterCriticalSection(&g_art_cs);
        if (a->state == 0 || _stricmp(a->src, full)) {
            char dir[MAX_PATH];
            if (a->part) {
                _snprintf(dir, sizeof dir, "%s\\%s", g_spch_dir, a->folder);
                CreateDirectoryA(dir, NULL);
            } else {
                _snprintf(dir, sizeof dir, "%s", g_art_dir);
            }
            _snprintf(a->dst, sizeof a->dst, "%s\\%s", dir, a->base);
            a->state = art_build_viv(full, a->dst, a->add_name, add, add_size, a->sorted) ? 1 : -1;
            _snprintf(a->src, sizeof a->src, "%s", full);
        }
        ready = a->state == 1;
        LeaveCriticalSection(&g_art_cs);
        if (!ready) return 0;
        _snprintf(out, cap, "%s", a->dst);
        return 1;
    }
    return 0;
}

/* called after roster_setup */
static void art_setup(void)
{
    char path[MAX_PATH];
    int want_art = ini_int("JordanArt", 1), want_spch = ini_int("JordanSpeech", 1), pack = 0, art = 0, spch = 0, jordan = 0, fi;
    int face = 0, port = 0, act = 0;
    InitializeCriticalSection(&g_art_cs);
    if (!g_roster_active) {
        LOG("Jordan art: roster copy not in use; default art");
        return;
    }
    for (fi = 0; fi < RM_NFILES; fi++)
        if (!_stricmp(g_rm_files[fi].name, "nba98_jordan.dat") && g_rm_files[fi].loaded) jordan = 1;
    _snprintf(path, sizeof path, "%smj.dat", g_exe_dir);
    if (!jordan) {
        LOG("Jordan art: Jordan's roster data is not in use; mj.dat not used");
    } else if (!want_art && !want_spch) {
        LOG("Jordan art: JordanArt=0 and JordanSpeech=0; default art and speech%s",
            GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ? " (mj.dat ignored)" : "");
    } else if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        LOG("Jordan art: no mj.dat in the game folder; default art and speech");
    } else {
        pack = art_load_pack(path);
    }
    if (pack) {
        _snprintf(g_art_dir, sizeof g_art_dir, "%s\\art", g_roster_root);
        _snprintf(g_spch_dir, sizeof g_spch_dir, "%s\\speech", g_roster_root);
        if (!want_art) LOG("Jordan art: JordanArt=0; default art");
        else if (!CreateDirectoryA(g_art_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            LOG("Jordan art: cannot create %s (error %lu); default art used", g_art_dir, GetLastError());
        else {
            /* the face, the headshot and the action shot are each used if mj.dat has them */
            face = art_face_ok();
            port = art_extract_pic(&k_pic_port);
            act = art_extract_pic(&k_pic_act);
            art = face || port || act;
            if (!art) LOG("Jordan art: mj.dat has no usable face, headshot or action shot; default art");
        }
        if (!want_spch) LOG("Jordan speech: JordanSpeech=0; REJORDA speech used");
        else if (spch_part_ok()) {
            if (!CreateDirectoryA(g_spch_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
                LOG("Jordan speech: cannot create %s (error %lu); REJORDA speech used", g_spch_dir, GetLastError());
            else
                spch = 1;
        }
    }
    if (face && port) art_face_portrait();
    else if (port) LOG("Jordan art: no face in mj.dat, so the in-match portrait (fouls, free throws) is the default face's");
    if (jordan) art_roster(face, port, act, spch);
    g_art_face = face;
    g_art_port = port;
    g_art_act = act;
    g_art_on = art;
    g_spch_on = spch;
    if (art || spch)
        LOG("Jordan art: using mj.dat (crc32 %08lx):%s%s%s%s", (unsigned long)g_art_pack_crc, face ? " face" : "", port ? " headshot" : "",
            act ? " action shot" : "", spch ? " speech (MIJORDA)" : "");
}
