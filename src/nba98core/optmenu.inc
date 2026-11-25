/*
 * optmenu.inc - our own settings screens in the game's front end (part of nba98core.dll,
 * included from nba98core.cpp). Roadmap item 30, decision D2.
 *
 * A new button on the Game Setup screen (the main menu), above Load, opens our screen.
 * Our screen is a new entry in the game's screen state machine and is built with the
 * game's own UI code, so it looks and behaves like the other menus:
 *   - The main menu's layout GSETUP.LAY gets one more button object ("NBA98Options").
 *     The modified copy, and the layouts of our own pages (NBAOPT<n>.LAY), are written to
 *     nba98data\menu\ at start-up and the CreateFileA hook serves them by file name. The
 *     game's own layout file is never changed.
 *   - Game Setup init (0x49D1E4, called at 0x4C428E) is wrapped: after it, our button is
 *     added with the game's add-button call (0x4C81C0), id OPT_BUTTON_ID.
 *   - Game Setup's event handler: an unknown button id ends at 0x49DD1F (jne 0x49DBD3,
 *     "ignore"). That jump goes to our stub instead: our id sets the screen's result to
 *     OPT_SCREEN_ID (the same way Rules/Options sets 7); anything else carries on to
 *     0x49DBD3 as before.
 *   - The dispatcher (0x4C3F78) jumps through a table of 0x49 entries at 0x4C3E38 by
 *     screen id. Id 0x1A is unused (points at the loop end); its entry now points at our
 *     handler, which runs our pages and returns to Game Setup (screen 4), doing the same
 *     bookkeeping as the game's own handlers (previous screen, history).
 *   - Our pages: a stack of pages (root, then sub pages). Each page is a screen built from
 *     its own layout: background, banner, a heading, a column of buttons. A button opens a
 *     sub page or goes back; Esc, the back arrow and the Back button go up one level.
 *     Every page change closes the screen and opens the next one, as the game does
 *     between its own screens.
 * All game functions use Watcom's register convention (EAX, EDX, EBX, ECX, then the
 * stack, callee pops); wcall() below makes those calls.
 * Settings: [nba98] OptionsMenu=1 (default). 0 leaves the main menu as it was.
 */

#include "nba98_stats.h"
extern nba98_stats g_stats __asm__("nba98_g_stats");   /* timing.inc */

#define OPT_BUTTON_ID 0x2F0          /* our button on Game Setup */
#define OPT_SCREEN_ID 0x1A           /* our entry in the screen table (unused by the game) */
#define OPT_ITEM_ID0 0x2E0           /* buttons on our pages: OPT_ITEM_ID0 + index */
#define EV_ACCEPT 0x0C               /* the forward arrow / Enter */
#define EV_BACK 0x0D                 /* the back arrow / Esc */

static int g_opt_on;
static char g_opt_dir[MAX_PATH];     /* <install>\nba98data\menu */

/* ---------------------------------------------------------------- calling game code */

/* Watcom register call: fn(eax, edx, ebx, ecx); returns eax */
uint32_t opt_wcall(uint32_t fn, uint32_t a, uint32_t d, uint32_t b, uint32_t c) __asm__("nba98_opt_wcall");
/* the same with four stack arguments (s1 nearest the return address); callee pops them */
uint32_t opt_wcall4(uint32_t fn, uint32_t a, uint32_t d, uint32_t b, uint32_t c, uint32_t s1, uint32_t s2, uint32_t s3,
                    uint32_t s4) __asm__("nba98_opt_wcall4");
/* the same with two stack arguments (s1 nearest the return address) */
uint32_t opt_wcall2(uint32_t fn, uint32_t a, uint32_t d, uint32_t b, uint32_t c, uint32_t s1, uint32_t s2) __asm__("nba98_opt_wcall2");
/* the same with one stack argument */
uint32_t opt_wcall1(uint32_t fn, uint32_t a, uint32_t d, uint32_t b, uint32_t c, uint32_t s1) __asm__("nba98_opt_wcall1");
__asm__(".text\n"
        "nba98_opt_wcall:\n"
        "  pushl %ebx\n  pushl %esi\n  pushl %edi\n  pushl %ebp\n"
        "  movl 20(%esp), %esi\n"
        "  movl 24(%esp), %eax\n  movl 28(%esp), %edx\n  movl 32(%esp), %ebx\n  movl 36(%esp), %ecx\n"
        "  call *%esi\n"
        "  popl %ebp\n  popl %edi\n  popl %esi\n  popl %ebx\n"
        "  ret\n"
        "nba98_opt_wcall4:\n"
        "  pushl %ebx\n  pushl %esi\n  pushl %edi\n  pushl %ebp\n"
        "  pushl 52(%esp)\n  pushl 52(%esp)\n  pushl 52(%esp)\n  pushl 52(%esp)\n"   /* s4, s3, s2, s1 */
        "  movl 36(%esp), %esi\n"
        "  movl 40(%esp), %eax\n  movl 44(%esp), %edx\n  movl 48(%esp), %ebx\n  movl 52(%esp), %ecx\n"
        "  call *%esi\n"
        "  popl %ebp\n  popl %edi\n  popl %esi\n  popl %ebx\n"
        "  ret\n"
        "nba98_opt_wcall2:\n"
        "  pushl %ebx\n  pushl %esi\n  pushl %edi\n  pushl %ebp\n"
        "  pushl 44(%esp)\n  pushl 44(%esp)\n"   /* s2, s1 */
        "  movl 28(%esp), %esi\n"
        "  movl 32(%esp), %eax\n  movl 36(%esp), %edx\n  movl 40(%esp), %ebx\n  movl 44(%esp), %ecx\n"
        "  call *%esi\n"
        "  popl %ebp\n  popl %edi\n  popl %esi\n  popl %ebx\n"
        "  ret\n"
        "nba98_opt_wcall1:\n"
        "  pushl %ebx\n  pushl %esi\n  pushl %edi\n  pushl %ebp\n"
        "  pushl 40(%esp)\n"
        "  movl 24(%esp), %esi\n"
        "  movl 28(%esp), %eax\n  movl 32(%esp), %edx\n  movl 36(%esp), %ebx\n  movl 40(%esp), %ecx\n"
        "  call *%esi\n"
        "  popl %ebp\n  popl %edi\n  popl %esi\n  popl %ebx\n"
        "  ret\n");

#define W0(fn) opt_wcall((fn), 0, 0, 0, 0)
#define GB(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define GD(a) (*(volatile uint32_t *)(uintptr_t)(a))
typedef void (__cdecl *ui_label_fn)(uint32_t screen, const char *obj, int a, int b, const char *text);

/* ---------------------------------------------------------------- pages */

/*
 * A page item either opens a page (target = page index, -1 = back) or is a setting row
 * (setting >= 0). A setting row reads "Name: value" and, like the game's Rules & Options
 * rows, moves to the next value each time it is chosen; the new value is written to
 * nba98.ini straight away and the row's text is changed with the game's own call (0x4C842C).
 */
static int lod_mode(void);              /* player_lod.inc: distance detail 0 original, 1 auto, 2 full */
static void lod_set_mode(int m);
static void skip_credits_apply(void);   /* skip_screens.inc: Game Setup's quit goes to the exit screen or the credits */

typedef struct { const char *label; int target; int setting; } opt_item;
typedef struct { const char *title, *heading, *note; const opt_item *items; int nitems; const char *note2; } opt_page;

enum { SET_RESOLUTION, SET_WINDOWED, SET_VSYNC, SET_JORDAN, SET_CUTPLAYERS, SET_JORDAN_ART, SET_JORDAN_HEAD,
       SET_JORDAN_ACT, SET_JORDAN_SPEECH, SET_MENU_PLAYER, SET_MENU_TINT, SET_CRT,
       SET_GAMEPADS, SET_DEADZONE, SET_STICK, SET_PADLAYOUT, SET_LATENCY, SET_PADMENUS, SET_WIDESCREEN, SET_LOD, SET_ALLSTAR,
       SET_SKIP_INTROS, SET_SKIP_CREDITS, SET_COUNT };

#define OPT_NOTE_RESTART "Changes take effect the next time the game starts."
static const opt_item k_root_items[] = {
    {"Display", 1, -1}, {"Gameplay", 2, -1}, {"Controls", 3, -1}, {"Sound", 5, -1}, {"User Interface", 4, -1},
    {"Back", -1, -1},
};
static const opt_item k_display_items[] = {
    {"Resolution", 0, SET_RESOLUTION}, {"Display Mode", 0, SET_WINDOWED}, {"VSync", 0, SET_VSYNC}, {"CRT Filter", 0, SET_CRT},
    {"Widescreen", 0, SET_WIDESCREEN}, {"Distance Detail", 0, SET_LOD}, {"Back", -1, -1},
};
static const opt_item k_gameplay_items[] = {
    {"Michael Jordan", 0, SET_JORDAN}, {"Cut Players", 0, SET_CUTPLAYERS}, {"All-Star Jerseys", 0, SET_ALLSTAR}, {"Back", -1, -1},
};
static const opt_item k_ui_items[] = {
    {"Menu Player", 0, SET_MENU_PLAYER}, {"Menu Colour", 0, SET_MENU_TINT},
    {"Jordan Art", 0, SET_JORDAN_ART}, {"Jordan Headshot", 0, SET_JORDAN_HEAD}, {"Jordan Action Shot", 0, SET_JORDAN_ACT},
    {"Jordan Speech", 0, SET_JORDAN_SPEECH}, {"Skip Intros", 0, SET_SKIP_INTROS}, {"Skip Exit Credits", 0, SET_SKIP_CREDITS},
    {"Back", -1, -1},
};
static const opt_item k_controls_items[] = {
    {"Gamepads", 0, SET_GAMEPADS}, {"Dead Zone", 0, SET_DEADZONE}, {"Stick", 0, SET_STICK}, {"Pad Layout", 0, SET_PADLAYOUT},
    {"Menu Navigation", 0, SET_PADMENUS}, {"Back", -1, -1},
};
static const opt_item k_sound_items[] = {
    {"Audio Latency", 0, SET_LATENCY}, {"Back", -1, -1},
};
static const opt_page k_pages[] = {
    {"Enhanced Options", "Settings", NULL, k_root_items, 6},
    {"Display", "Display", OPT_NOTE_RESTART, k_display_items, 7, "CRT Filter and Distance Detail apply at once."},
    {"Gameplay", "Gameplay", OPT_NOTE_RESTART, k_gameplay_items, 4},
    {"Controls", "Controls", "Gamepads and Pad Layout apply at the next start.", k_controls_items, 6,
     "The other settings apply at once."},
    {"User Interface", "User Interface", "Jordan and intro settings apply on restart.", k_ui_items, 9,
     "Menu player and colour apply on the next screen."},
    {"Sound", "Sound", OPT_NOTE_RESTART, k_sound_items, 2, "Lower is less delay; raise it if the sound crackles."},
};
#define OPT_NPAGES ((int)(sizeof k_pages / sizeof k_pages[0]))

/* write one page's layout: same art and placement style as Game Setup */
static int opt_write_page_lay(int p)
{
    char path[MAX_PATH], buf[8192];
    const opt_page *pg = &k_pages[p];
    int len = 0, i, n = 3 + pg->nitems + (pg->note != NULL) + (pg->note2 != NULL);
#define ADD(...) (len += _snprintf(buf + len, sizeof buf - len, __VA_ARGS__))
    ADD("; NBA Live 98 fix: our settings page %d (generated by nba98core.dll)\r\n\r\n[screen]\r\nnumobjects=%d\r\n\r\n", p, n);
    ADD("[object 1]\r\nobjecttype=0\r\nobjectname=background\r\nxposition=0\r\nyposition=0\r\nwidth=640\r\nheight=480\r\n"
        "vpath=..\\..\\BACKGRND\\FEBGS\\\r\nfname=1GAMESET.FSH\r\nsname=0000\r\n\r\n");
    ADD("[object 2]\r\nobjecttype=5\r\nobjectname=banner\r\nxposition=29\r\nyposition=21\r\nwidth=211\r\nheight=67\r\n"
        "vpath=..\\..\\FOREGRND\\PLACHOLD\\\r\nfname=BANNER.FSH\r\nsname=bann\r\ntext=%s\r\n\r\n", pg->title);
    ADD("[object 3]\r\nobjecttype=10\r\nobjectname=Heading\r\nxposition=48\r\nyposition=125\r\nwidth=12\r\nheight=12\r\n"
        "vpath=..\\..\\FOREGRND\\ICONS\\\r\nfname=IDOT.FSH\r\nsname=icon\r\ntext=%s\r\nfont=14\r\nalign=left\r\n\r\n", pg->heading);
    for (i = 0; i < pg->nitems; i++)
        ADD("[object %d]\r\nobjecttype=1\r\nobjectname=Item%d\r\nxposition=64\r\nyposition=%d\r\nwidth=12\r\nheight=12\r\n"
            "vpath=..\\..\\FOREGRND\\ICONS\\\r\nfname=IDOT.FSH\r\nsname=icon\r\ntext=%s\r\nfont=12\r\nalign=left\r\n\r\n",
            4 + i, i, 148 + 18 * i, pg->items[i].label);
    if (pg->note)
        ADD("[object %d]\r\nobjecttype=10\r\nobjectname=Note\r\nxposition=64\r\nyposition=%d\r\nwidth=12\r\nheight=12\r\n"
            "vpath=..\\..\\FOREGRND\\ICONS\\\r\nfname=IDOT.FSH\r\nsname=icon\r\ntext=%s\r\nfont=12\r\nalign=left\r\n\r\n",
            4 + pg->nitems, 148 + 18 * pg->nitems + 14, pg->note);
    if (pg->note2)
        ADD("[object %d]\r\nobjecttype=10\r\nobjectname=Note2\r\nxposition=64\r\nyposition=%d\r\nwidth=12\r\nheight=12\r\n"
            "vpath=..\\..\\FOREGRND\\ICONS\\\r\nfname=IDOT.FSH\r\nsname=icon\r\ntext=%s\r\nfont=12\r\nalign=left\r\n\r\n",
            5 + pg->nitems, 148 + 18 * pg->nitems + 30, pg->note2);
#undef ADD
    if (len >= (int)sizeof buf - 1) return 0;
    _snprintf(path, sizeof path, "%s\\NBAOPT%d.LAY", g_opt_dir, p);
    return rm_write_all(path, buf, (DWORD)len, NULL, 0, NULL, 0);
}

/* the main menu layout with our button added above Load */
static int opt_write_gsetup_lay(void)
{
    char src[MAX_PATH], dst[MAX_PATH];
    static const char add[] =
        "\r\n[object %d]\r\nobjecttype=1\r\nobjectname=NBA98Options\r\nxposition=330\r\nyposition=347\r\nwidth=12\r\nheight=12\r\n"
        "vpath=..\\..\\FOREGRND\\ICONS\\\r\nfname=IDOT.FSH\r\nsname=icon\r\ntext=Enhanced Options\r\nfont=12\r\nalign=left\r\n\r\n";
    DWORD n = 0;
    unsigned char *d;
    char *p, *out, extra[512];
    int count, len, ok;
    _snprintf(src, sizeof src, "%sart\\frontend\\layout\\layoute\\GSETUP.LAY", g_exe_dir);
    d = rm_read_all(src, &n);
    if (!d) { LOG("options menu: cannot read %s", src); return 0; }
    d[n] = 0;
    p = strstr((char *)d, "numobjects=");
    if (!p) { LOG("options menu: %s has no numobjects line", src); HeapFree(GetProcessHeap(), 0, d); return 0; }
    count = atoi(p + 11);
    out = (char *)HeapAlloc(GetProcessHeap(), 0, n + 1024);
    if (!out) { HeapFree(GetProcessHeap(), 0, d); return 0; }
    len = (int)(p - (char *)d);
    memcpy(out, d, len);
    len += sprintf(out + len, "numobjects=%d", count + 1);
    p += 11;
    while (*p >= '0' && *p <= '9') p++;
    memcpy(out + len, p, n - (DWORD)(p - (char *)d));
    len += (int)(n - (DWORD)(p - (char *)d));
    _snprintf(extra, sizeof extra, add, count + 1);
    memcpy(out + len, extra, strlen(extra));
    len += (int)strlen(extra);
    _snprintf(dst, sizeof dst, "%s\\GSETUP.LAY", g_opt_dir);
    ok = rm_write_all(dst, out, (DWORD)len, NULL, 0, NULL, 0);
    HeapFree(GetProcessHeap(), 0, out);
    HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

/* ---------------------------------------------------------------- the main menu's line drawing */
/*
 * LINEMAP.VIV (art\frontend\foregrnd\etextart) holds the screens' line drawings in EA's
 * compact archive format: C0 FB, u16 BE (directory end - 4), u16 BE count, then per entry a
 * 3-byte BE offset, 3-byte BE size and the name; data 4-byte aligned. Each .lin is
 * RefPack-compressed (10 FB) to 10100 bytes: 10 groups of 10 records of 101 bytes, all
 * 0xFF when unused. A record: +0 type (direction), +1 x, +3 y (u16 LE start), +5 x, +7 y,
 * +9 u16 length, +11, +12 speed, +13, then the dots from +14 (5 bytes each: x, y u16 LE,
 * flag), +64 u16 dot count twice.
 * In gsetup.lin, record 2 is the vertical line that rises from the bottom (319,479) to
 * the dots of Load, Rules/Options, Stats and Rosters (x 320, y 371 to 427). We lengthen
 * it to y 352 and add a dot there for our button (y 347, like Load at 366 and its dot at 371).
 */
static unsigned char *refpack_unpack(const unsigned char *s, DWORD n, DWORD *outn)
{
    DWORD p, size, o = 0;
    unsigned char *out;
    int nb;
    if (n < 5 || s[1] != 0xFB) return NULL;
    nb = (s[0] & 0x80) ? 4 : 3;
    p = 2 + ((s[0] & 0x01) ? nb : 0);
    size = 0;
    for (; nb; nb--) size = (size << 8) | s[p++];
    out = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, size + 8);
    if (!out) return NULL;
    while (p < n && o <= size) {
        unsigned b0 = s[p], lit, len = 0, off = 0, k;
        if (b0 < 0x80) {
            unsigned b1 = s[p + 1]; p += 2; lit = b0 & 3;
            len = ((b0 >> 2) & 7) + 3; off = ((b0 & 0x60) << 3) + b1 + 1;
        } else if (b0 < 0xC0) {
            unsigned b1 = s[p + 1], b2 = s[p + 2]; p += 3; lit = b1 >> 6;
            len = (b0 & 0x3F) + 4; off = ((b1 & 0x3F) << 8) + b2 + 1;
        } else if (b0 < 0xE0) {
            unsigned b1 = s[p + 1], b2 = s[p + 2], b3 = s[p + 3]; p += 4; lit = b0 & 3;
            len = ((b0 & 0x0C) << 6) + b3 + 5; off = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1;
        } else if (b0 < 0xFC) {
            p++; lit = ((b0 & 0x1F) << 2) + 4;
        } else {
            p++; lit = b0 & 3;
        }
        if (o + lit + len > size || p + lit > n || off > o + lit) break;
        memcpy(out + o, s + p, lit); p += lit; o += lit;
        for (k = 0; k < len; k++, o++) out[o] = out[o - off];
        if (b0 >= 0xFC) break;
    }
    if (o != size) { HeapFree(GetProcessHeap(), 0, out); return NULL; }
    *outn = size;
    return out;
}

/* RefPack with literal blocks only (valid for any RefPack reader); returns the length */
static DWORD refpack_store(const unsigned char *s, DWORD n, unsigned char *out)
{
    DWORD o = 0, p = 0;
    out[o++] = 0x10; out[o++] = 0xFB;
    out[o++] = (unsigned char)(n >> 16); out[o++] = (unsigned char)(n >> 8); out[o++] = (unsigned char)n;
    while (n - p >= 4) {
        DWORD k = n - p >= 112 ? 112 : ((n - p) & ~3u);
        out[o++] = (unsigned char)(0xE0 + (k - 4) / 4);
        memcpy(out + o, s + p, k); o += k; p += k;
    }
    out[o++] = (unsigned char)(0xFC + (n - p));
    memcpy(out + o, s + p, n - p); o += n - p;
    return o;
}

static int opt_write_linemap(void)
{
    /* the v1.1 English gsetup.lin, unpacked: we only change a file we know exactly (no EA bytes kept here) */
    static const DWORD k_lin_size = 10100, k_lin_crc = 0xA44F4B84;
    char src[MAX_PATH], dst[MAX_PATH];
    unsigned char *d, *lin = NULL, *out = NULL;
    DWORD n = 0, linn = 0, hs, cnt, q, i;
    int ok = 0, gi = -1;
    struct { char name[32]; DWORD off, size; } e[64];
    _snprintf(src, sizeof src, "%sart\\frontend\\foregrnd\\etextart\\LINEMAP.VIV", g_exe_dir);
    d = rm_read_all(src, &n);
    if (!d) { LOG("options menu: cannot read %s; main menu lines unchanged", src); return 0; }
    if (n < 6 || d[0] != 0xC0 || d[1] != 0xFB) goto bad;
    hs = (d[2] << 8) | d[3];
    cnt = (d[4] << 8) | d[5];
    if (cnt > 64 || hs > n) goto bad;
    hs = n;   /* the size field is not a bound: EA stores the directory end minus 4 */
    for (q = 6, i = 0; i < cnt; i++) {
        const unsigned char *z;
        if (q + 7 > hs) goto bad;
        e[i].off = (d[q] << 16) | (d[q + 1] << 8) | d[q + 2];
        e[i].size = (d[q + 3] << 16) | (d[q + 4] << 8) | d[q + 5];
        z = (const unsigned char *)memchr(d + q + 6, 0, hs - (q + 6));
        if (!z || z - (d + q + 6) >= 32 || e[i].off + e[i].size > n) goto bad;
        memcpy(e[i].name, d + q + 6, z - (d + q + 6) + 1);
        q = (DWORD)(z - d) + 1;
        if (!_stricmp(e[i].name, "gsetup.lin")) gi = (int)i;
    }
    if (gi < 0) goto bad;
    lin = refpack_unpack(d + e[gi].off, e[gi].size, &linn);
    if (!lin || linn != k_lin_size || crc32_buf(lin, linn) != k_lin_crc) {
        LOG("options menu: gsetup.lin is not as expected; main menu lines unchanged");
        goto done;
    }
    /* record 2: rise to y 352, our dot first (the dots run top to bottom), then the four */
    lin[202 + 9] = 0x7F;
    memmove(lin + 202 + 19, lin + 202 + 14, 20);
    lin[216] = 0x40; lin[217] = 0x01; lin[218] = 0x60; lin[219] = 0x01; lin[220] = 0x00;
    lin[266] = 5; lin[268] = 5;
    {
        /* rebuild the archive with the changed entry stored */
        DWORD cap = n + linn + linn / 112 + 64, o, off;
        unsigned char *packed = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, linn + linn / 112 + 16);
        DWORD packn;
        out = (unsigned char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
        if (!out || !packed) { if (packed) HeapFree(GetProcessHeap(), 0, packed); goto done; }
        packn = refpack_store(lin, linn, packed);
        o = 6;
        for (i = 0; i < cnt; i++) o += 6 + (DWORD)strlen(e[i].name) + 1;
        off = (o + 3) & ~3u;
        hs = off - 4;   /* as EA writes it (directory end 740, field 736) */
        out[0] = 0xC0; out[1] = 0xFB; out[2] = (unsigned char)(hs >> 8); out[3] = (unsigned char)hs;
        out[4] = (unsigned char)(cnt >> 8); out[5] = (unsigned char)cnt;
        for (q = 6, i = 0; i < cnt; i++) {
            DWORD sz = (int)i == gi ? packn : e[i].size;
            size_t ln = strlen(e[i].name) + 1;
            out[q] = (unsigned char)(off >> 16); out[q + 1] = (unsigned char)(off >> 8); out[q + 2] = (unsigned char)off;
            out[q + 3] = (unsigned char)(sz >> 16); out[q + 4] = (unsigned char)(sz >> 8); out[q + 5] = (unsigned char)sz;
            memcpy(out + q + 6, e[i].name, ln);
            q += 6 + (DWORD)ln;
            memcpy(out + off, (int)i == gi ? packed : d + e[i].off, sz);
            off = (off + sz + 3) & ~3u;
        }
        HeapFree(GetProcessHeap(), 0, packed);
        _snprintf(dst, sizeof dst, "%s\\LINEMAP.VIV", g_opt_dir);
        ok = rm_write_all(dst, out, off, NULL, 0, NULL, 0);
    }
    goto done;
bad:
    LOG("options menu: %s is not as expected; main menu lines unchanged", src);
done:
    if (out) HeapFree(GetProcessHeap(), 0, out);
    if (lin) HeapFree(GetProcessHeap(), 0, lin);
    HeapFree(GetProcessHeap(), 0, d);
    return ok;
}

static int g_opt_lines;   /* our LINEMAP.VIV is ready */

/* CreateFileA: serve our layouts by name */
static int opt_redirect(const char *name, char *out, size_t cap)
{
    const char *b;
    if (!g_opt_on || !name) return 0;
    b = art_base(name);
    if (!_stricmp(b, "GSETUP.LAY") || (!_strnicmp(b, "NBAOPT", 6) && strlen(b) <= 12) ||
        (g_opt_lines && !_stricmp(b, "LINEMAP.VIV"))) {
        _snprintf(out, cap, "%s\\%s", g_opt_dir, b);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- settings in nba98.ini */

/*
 * Set key=value in one section of nba98.ini, keeping every comment and the order of the
 * file: an existing "key=" line gets the new value; otherwise the commented example line
 * ";key=" is made active; otherwise the line is added at the end of the section (or a new
 * section at the end of the file). Written through a .new file and a rename.
 */
static int opt_ini_set(const char *section, const char *key, const char *value)
{
    DWORD n = 0;
    unsigned char *d = rm_read_all(g_ini, &n);
    char *s, *out, *line, *end, *sec_end = NULL, *hit = NULL, *hit_end = NULL, *comment = NULL, *comment_end = NULL;
    size_t kl = strlen(key), sl = strlen(section);
    int in_sec = 0, len, ok;
    char add[160];
    if (!d) { d = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, 1); n = 0; if (!d) return 0; }
    d[n] = 0;
    s = (char *)d;
    for (line = s; *line; line = end) {
        char *t = line;
        end = strchr(line, '\n');
        end = end ? end + 1 : line + strlen(line);
        while (t < end && (*t == ' ' || *t == '\t')) t++;
        if (*t == '[') {
            if (in_sec) { sec_end = line; break; }
            in_sec = !_strnicmp(t + 1, section, sl) && t[1 + sl] == ']';
            continue;
        }
        if (!in_sec) continue;
        if (!_strnicmp(t, key, kl) && (t[kl] == '=' || t[kl] == ' ')) { hit = line; hit_end = end; break; }
        if (*t == ';' && !comment && !_strnicmp(t + 1, key, kl) && t[1 + kl] == '=') { comment = line; comment_end = end; }
    }
    if (in_sec && !sec_end) sec_end = s + n;
    if (!hit && comment) { hit = comment; hit_end = comment_end; }
    _snprintf(add, sizeof add, "%s=%s\r\n", key, value);
    out = (char *)HeapAlloc(GetProcessHeap(), 0, n + sizeof add + sl + 16);
    if (!out) { HeapFree(GetProcessHeap(), 0, d); return 0; }
    if (hit) {                                   /* replace that line */
        len = (int)(hit - s);
        memcpy(out, s, len);
        len += sprintf(out + len, "%s", add);
        memcpy(out + len, hit_end, n - (hit_end - s));
        len += (int)(n - (hit_end - s));
    } else if (sec_end) {                        /* add at the end of the section, before its blank lines */
        char *at = sec_end;
        for (;;) {                               /* step back over blank lines */
            char *q = at, *ls;
            if (q > s && q[-1] == '\n') q--;
            if (q > s && q[-1] == '\r') q--;
            for (ls = q; ls > s && ls[-1] != '\n'; ls--) {}
            if (ls == q && ls < at && ls > s) { at = ls; continue; }
            break;
        }
        len = (int)(at - s);
        memcpy(out, s, len);
        if (len && out[len - 1] != '\n') { out[len++] = '\r'; out[len++] = '\n'; }
        len += sprintf(out + len, "%s", add);
        memcpy(out + len, at, n - (at - s));
        len += (int)(n - (at - s));
    } else {                                     /* no such section yet */
        len = (int)n;
        memcpy(out, s, n);
        if (len && out[len - 1] != '\n') { out[len++] = '\r'; out[len++] = '\n'; }
        len += sprintf(out + len, "\r\n[%s]\r\n%s", section, add);
    }
    ok = rm_write_all(g_ini, out, (DWORD)len, NULL, 0, NULL, 0);
    HeapFree(GetProcessHeap(), 0, out);
    HeapFree(GetProcessHeap(), 0, d);
    LOG("options menu: [%s] %s=%s %s", section, key, value, ok ? "saved" : "NOT saved (nba98.ini could not be written)");
    return ok;
}

static int opt_glide_int(const char *key, int def) { return (int)GetPrivateProfileIntA("glide", key, def, g_ini); }

/* CRT filter ([glide] CRT): applies at once, through the shared stats (glide2x reads crt_mode every frame) */
static const char *const k_crt[4] = {"Off", "Auto", "Sharp", "Soft"};
static const char *const k_crt_ini[4] = {"off", "auto", "sharp", "soft"};
static int opt_crt(void)
{
    char v[16];
    int i;
    GetPrivateProfileStringA("glide", "CRT", "off", v, sizeof v, g_ini);
    for (i = 0; i < 4; i++) if (!_stricmp(v, k_crt_ini[i]) || (v[0] == '0' + i && !v[1])) return i;
    return 0;
}

/* Controls and Sound pages (0.7.3): [input] and [sound] in nba98.ini */
static void pad_reload_live(void);   /* pad_input.inc: re-reads Deadzone and Stick */
static void pad_reload_menus(void);  /* pad_menu.inc: re-reads PadMenus */
static const int k_deadzone[] = {10, 15, 20, 25, 30};
#define OPT_NDEADZONE ((int)(sizeof k_deadzone / sizeof k_deadzone[0]))
static const int k_latency[] = {20, 40, 60, 80};
#define OPT_NLATENCY ((int)(sizeof k_latency / sizeof k_latency[0]))
static int opt_input_int(const char *key, int def) { return (int)GetPrivateProfileIntA("input", key, def, g_ini); }
static int opt_input_is(const char *key, const char *def, const char *what)
{
    char v[16];
    GetPrivateProfileStringA("input", key, def, v, sizeof v, g_ini);
    return !_stricmp(v, what);
}
/* next value in a list after cur (a value typed into the ini by hand goes on from the next bigger one) */
static int opt_next_in(const int *list, int n, int cur)
{
    int i;
    for (i = 0; i < n; i++) if (list[i] > cur) return list[i];
    return list[0];
}

/* 4:3 sizes offered for Resolution; index 0 is Auto (Width=0, Height=0) */
static const int k_res[][2] = {
    {0, 0}, {640, 480}, {800, 600}, {1024, 768}, {1280, 960}, {1440, 1080}, {1600, 1200}, {1920, 1440}, {2560, 1920}, {2880, 2160},
};
#define OPT_NRES ((int)(sizeof k_res / sizeof k_res[0]))

/* what Auto means: in full screen the 4:3 part of the monitor, in a window 1280x960 (as glide2x.dll does it) */
static void opt_auto_res(int windowed, int *w, int *h)
{
    int mw = GetSystemMetrics(SM_CXSCREEN), mh = GetSystemMetrics(SM_CYSCREEN);
    if (windowed || mw <= 0 || mh <= 0) { *w = 1280; *h = 960; return; }
    if (mw * 3 >= mh * 4) { *h = mh; *w = mh * 4 / 3; } else { *w = mw; *h = mw * 3 / 4; }
}

/*
 * Menu look (project doc nba98-menu-backdrops). At start-up the game picks a colour tint
 * (rand() % 11, a COLORREF dword at 0x619879) and a player photo set (rand() % 5 + 1, word at
 * 0x61987D); every front-end screen reads both again when it loads. [menu] Tint and Player
 * pin them; "random" keeps the game's pick. "grey" (no tint) is new. A change in our menu is
 * written at once, so it shows on the next screen.
 */
typedef struct { const char *ini, *name; uint32_t tint; } opt_tint;
/* names by how each looks on screen: the game's tint dword is 0x00RRGGBB here (blue in the byte at 0x619879),
   checked in Wine against the main menu (2026-10-30) */
static const opt_tint k_tints[] = {   /* menu order, by hue; [0] = random; comment = the game's index */
    {"random", "Random", 0}, {"grey", "Grey", 0xFFFFFFFF},
    {"rose", "Rose", 0xFFFFA5D0} /* 8 */, {"pink", "Pink", 0xFFFF94D6} /* 4 */, {"orchid", "Orchid", 0xFFFCC0FF} /* 3 */,
    {"lavender", "Lavender", 0xFFC6BDFF} /* 10 */, {"blue", "Blue", 0xFFB3B2FF} /* 2 */, {"skyblue", "Sky Blue", 0xFFB7F5FF} /* 1 */,
    {"teal", "Teal", 0xFFB7FFF5} /* 7 */, {"mint", "Mint", 0xFFBEFFE0} /* 0 */, {"green", "Green", 0xFFA0FFA0} /* 6 */,
    {"gold", "Gold", 0xFFFFD5A5} /* 5 */, {"copper", "Copper", 0xFFFFB096} /* 9 */,
};
#define OPT_NTINT ((int)(sizeof k_tints / sizeof k_tints[0]))
static const char *const k_players[6] = {"Random", "Mitch Richmond", "Larry Johnson", "Christian Laettner", "Tim Hardaway",
                                         "Joe Dumars"};
static const char *const k_player_ini[6] = {"random", "richmond", "johnson", "laettner", "hardaway", "dumars"};

static int opt_look_tint(void)   /* index into k_tints, 0 = random */
{
    char v[32];
    int i;
    GetPrivateProfileStringA("menu", "Tint", "random", v, sizeof v, g_ini);
    for (i = 0; i < OPT_NTINT; i++) if (!_stricmp(v, k_tints[i].ini)) return i;
    return 0;
}

static int opt_look_player(void)   /* 1 to 5, 0 = random */
{
    char v[32];
    int i, n;
    GetPrivateProfileStringA("menu", "Player", "random", v, sizeof v, g_ini);
    n = atoi(v);
    if (n >= 1 && n <= 5) return n;
    for (i = 1; i < 6; i++) if (!_stricmp(v, k_player_ini[i])) return i;
    return 0;
}

/* write the pinned values into the game (after its start-up picks, and on a change in our menu) */
static void opt_look_write(int log)
{
    int t = opt_look_tint(), pl = opt_look_player();
    if (log) LOG("menu look: game picked tint 0x%08lx, player set %d", (unsigned long)GD(0x619879), (int)*(volatile int16_t *)0x61987D);
    if (t) GD(0x619879) = k_tints[t].tint;
    if (pl) *(volatile int16_t *)0x61987D = (int16_t)pl;
    if (log && (t || pl)) LOG("menu look: pinned%s%s%s%s", t ? " colour " : "", t ? k_tints[t].name : "", pl ? ", player " : "",
                              pl ? k_players[pl] : "");
}

void opt_look_hook(void) __asm__("nba98_opt_look_hook");
void opt_look_hook(void) { opt_look_write(1); }
void opt_look_thunk(void) __asm__("nba98_opt_look_thunk");
__asm__(".text\n"
        /* replaces "call 0x522AF4" at 0x42F4E2, right after the start-up tint and player picks */
        "nba98_opt_look_thunk:\n"
        "  pushal\n"
        "  call nba98_opt_look_hook\n"
        "  popal\n"
        "  pushl $0x522AF4\n"
        "  ret\n");

/* JordanHeadshot / JordanActionShot as set in nba98.ini: 0 = random, else the picture number */
static int opt_pic_value(int set)
{
    char v[16];
    int n;
    GetPrivateProfileStringA("nba98", set == SET_JORDAN_ACT ? "JordanActionShot" : "JordanHeadshot", "random", v, sizeof v, g_ini);
    n = atoi(v);
    return n >= 1 && n <= 9 ? n : 0;
}

/* position of picture number k among the usable ones (1 = first) */
static int opt_pic_ordinal(int kind, int k)
{
    int i, n = 0;
    for (i = 1; i <= k && i <= 9; i++) n += g_art_pic_ok[kind][i];
    return n ? n : k;
}

/* the row text for a setting, from what nba98.ini holds now */
static void opt_setting_text(int set, char *buf, size_t cap)
{
    int w = opt_glide_int("Width", 0), h = opt_glide_int("Height", 0), win = opt_glide_int("Windowed", 0) != 0;
    switch (set) {
    case SET_RESOLUTION:
        if (w > 0 && h > 0) _snprintf(buf, cap, "Resolution: %dx%d", w, h);
        else { opt_auto_res(win, &w, &h); _snprintf(buf, cap, "Resolution: Auto (%dx%d)", w, h); }
        break;
    case SET_WINDOWED:
        _snprintf(buf, cap, "Display Mode: %s", win ? "Window" : "Full Screen");
        break;
    case SET_VSYNC:
        _snprintf(buf, cap, "VSync: %s", opt_glide_int("VSync", 0) ? "On" : "Off");
        break;
    case SET_JORDAN:
        _snprintf(buf, cap, "Michael Jordan: %s", ini_int("Jordan", 1) ? "On" : "Off");
        break;
    case SET_CUTPLAYERS:
        _snprintf(buf, cap, "Cut Players: %s", ini_int("CutPlayers", 1) ? "On" : "Off");
        break;
    case SET_ALLSTAR:
        _snprintf(buf, cap, "All-Star Jerseys: %s", ini_int("AllStarJerseys", 1) ? "1997 Clubs" : "Original");
        break;
    case SET_SKIP_INTROS:
        _snprintf(buf, cap, "Skip Intros: %s", ini_int("SkipIntros", 1) ? "On" : "Off");
        break;
    case SET_SKIP_CREDITS:
        _snprintf(buf, cap, "Skip Exit Credits: %s", ini_int("SkipCredits", 1) ? "On" : "Off");
        break;
    case SET_JORDAN_ART:
        _snprintf(buf, cap, "Jordan Art: %s", ini_int("JordanArt", 1) ? "On" : "Off");
        break;
    case SET_JORDAN_SPEECH:
        _snprintf(buf, cap, "Jordan Speech: %s", ini_int("JordanSpeech", 1) ? "On" : "Off");
        break;
    case SET_JORDAN_HEAD:
    case SET_JORDAN_ACT: {
        int kind = set == SET_JORDAN_ACT, k, n = 0, v = opt_pic_value(set);
        const char *what = kind ? "Jordan Action Shot" : "Jordan Headshot";
        for (k = 1; k <= 9; k++) n += g_art_pic_ok[kind][k];
        if (!n) _snprintf(buf, cap, "%s: none in mj.dat", what);
        else if (!v) _snprintf(buf, cap, "%s: Random (%d)", what, n);
        else _snprintf(buf, cap, "%s: %d of %d", what, opt_pic_ordinal(kind, v), n);
        break;
    }
    case SET_MENU_PLAYER:
        _snprintf(buf, cap, "Menu Player: %s", k_players[opt_look_player()]);
        break;
    case SET_MENU_TINT:
        _snprintf(buf, cap, "Menu Colour: %s", k_tints[opt_look_tint()].name);
        break;
    case SET_CRT:
        _snprintf(buf, cap, "CRT Filter: %s", k_crt[opt_crt()]);
        break;
    case SET_GAMEPADS:
        _snprintf(buf, cap, "Gamepads: %s", opt_input_int("Gamepads", 1) ? "On" : "Off (Windows joysticks)");
        break;
    case SET_DEADZONE:
        _snprintf(buf, cap, "Dead Zone: %d%%", opt_input_int("Deadzone", 20));
        break;
    case SET_STICK:
        _snprintf(buf, cap, "Stick: %s", opt_input_is("Stick", "8way", "raw") ? "Raw" : "8-Way");
        break;
    case SET_PADLAYOUT:
        _snprintf(buf, cap, "Pad Layout: %s", opt_input_is("PadLayout", "ps1", "ps1") ? "PlayStation" : "PC");
        break;
    case SET_WIDESCREEN:
        _snprintf(buf, cap, "Widescreen: %s (Experimental)", opt_glide_int("Widescreen", 0) ? "On" : "Off");
        break;
    case SET_LOD: {
        static const char *const k_lod_txt[3] = {"Original", "Auto", "Full"};
        _snprintf(buf, cap, "Distance Detail: %s", k_lod_txt[lod_mode()]);
        break;
    }
    case SET_PADMENUS:
        _snprintf(buf, cap, "Menu Navigation: %s", opt_input_int("PadMenus", 1) ? "On" : "Off");
        break;
    case SET_LATENCY:
        _snprintf(buf, cap, "Audio Latency: %d ms", (int)GetPrivateProfileIntA("sound", "LatencyMs", 40, g_ini));
        break;
    default:
        buf[0] = 0;
    }
    buf[cap - 1] = 0;
}

/* move a setting to its next value and save it */
static void opt_setting_next(int set)
{
    char v[16];
    int win = opt_glide_int("Windowed", 0) != 0;
    switch (set) {
    case SET_RESOLUTION: {
        int w = opt_glide_int("Width", 0), h = opt_glide_int("Height", 0), i, cur = -1, mh = GetSystemMetrics(SM_CYSCREEN);
        if (w <= 0 || h <= 0) cur = 0;
        for (i = 1; i < OPT_NRES && cur < 0; i++) if (k_res[i][0] == w && k_res[i][1] == h) cur = i;
        if (cur < 0)   /* a size typed into the ini by hand: go on from the next bigger one in the list */
            for (cur = 0; cur + 1 < OPT_NRES && k_res[cur + 1][1] <= h; cur++) {}
        for (i = 1; i <= OPT_NRES; i++) {
            int k = (cur + i) % OPT_NRES;
            /* a window must fit on the screen (with room for its frame) */
            if (k && win && mh > 0 && k_res[k][1] > mh - 80) continue;
            cur = k;
            break;
        }
        _snprintf(v, sizeof v, "%d", k_res[cur][0]); opt_ini_set("glide", "Width", v);
        _snprintf(v, sizeof v, "%d", k_res[cur][1]); opt_ini_set("glide", "Height", v);
        break;
    }
    case SET_WINDOWED:
        opt_ini_set("glide", "Windowed", win ? "0" : "1");
        break;
    case SET_VSYNC:
        opt_ini_set("glide", "VSync", opt_glide_int("VSync", 0) ? "0" : "1");
        break;
    case SET_MENU_PLAYER:
        opt_ini_set("menu", "Player", k_player_ini[(opt_look_player() + 1) % 6]);
        opt_look_write(0);
        break;
    case SET_MENU_TINT:
        opt_ini_set("menu", "Tint", k_tints[(opt_look_tint() + 1) % OPT_NTINT].ini);
        opt_look_write(0);
        break;
    case SET_CRT: {
        int m = (opt_crt() + 1) % 4;
        opt_ini_set("glide", "CRT", k_crt_ini[m]);
        g_stats.crt_mode = m;
        break;
    }
    case SET_GAMEPADS: opt_ini_set("input", "Gamepads", opt_input_int("Gamepads", 1) ? "0" : "1"); break;
    case SET_DEADZONE:
        _snprintf(v, sizeof v, "%d", opt_next_in(k_deadzone, OPT_NDEADZONE, opt_input_int("Deadzone", 20)));
        opt_ini_set("input", "Deadzone", v);
        pad_reload_live();
        break;
    case SET_STICK:
        opt_ini_set("input", "Stick", opt_input_is("Stick", "8way", "raw") ? "8way" : "raw");
        pad_reload_live();
        break;
    case SET_WIDESCREEN:
        opt_ini_set("glide", "Widescreen", opt_glide_int("Widescreen", 0) ? "0" : "1");
        break;
    case SET_LOD:
        lod_set_mode((lod_mode() + 1) % 3);
        break;
    case SET_PADMENUS:
        opt_ini_set("input", "PadMenus", opt_input_int("PadMenus", 1) ? "0" : "1");
        pad_reload_menus();
        break;
    case SET_PADLAYOUT: opt_ini_set("input", "PadLayout", opt_input_is("PadLayout", "ps1", "ps1") ? "pc" : "ps1"); break;
    case SET_LATENCY:
        _snprintf(v, sizeof v, "%d", opt_next_in(k_latency, OPT_NLATENCY, (int)GetPrivateProfileIntA("sound", "LatencyMs", 40, g_ini)));
        opt_ini_set("sound", "LatencyMs", v);
        break;
    case SET_JORDAN: opt_ini_set("nba98", "Jordan", ini_int("Jordan", 1) ? "0" : "1"); break;
    case SET_CUTPLAYERS: opt_ini_set("nba98", "CutPlayers", ini_int("CutPlayers", 1) ? "0" : "1"); break;
    case SET_ALLSTAR: opt_ini_set("nba98", "AllStarJerseys", ini_int("AllStarJerseys", 1) ? "0" : "1"); break;
    case SET_SKIP_INTROS: opt_ini_set("nba98", "SkipIntros", ini_int("SkipIntros", 1) ? "0" : "1"); break;
    case SET_SKIP_CREDITS:
        opt_ini_set("nba98", "SkipCredits", ini_int("SkipCredits", 1) ? "0" : "1");
        skip_credits_apply();
        break;
    case SET_JORDAN_ART: opt_ini_set("nba98", "JordanArt", ini_int("JordanArt", 1) ? "0" : "1"); break;
    case SET_JORDAN_SPEECH: opt_ini_set("nba98", "JordanSpeech", ini_int("JordanSpeech", 1) ? "0" : "1"); break;
    case SET_JORDAN_HEAD:
    case SET_JORDAN_ACT: {
        /* Random, then each picture mj.dat has (by its number), then Random again */
        int kind = set == SET_JORDAN_ACT, k, v = opt_pic_value(set), next = 0;
        for (k = v + 1; k <= 9 && !next; k++) if (g_art_pic_ok[kind][k]) next = k;
        if (!next && !v) break;   /* none in the pack: nothing to cycle */
        {
            char val[16];
            if (next) _snprintf(val, sizeof val, "%d", next); else strcpy(val, "random");
            opt_ini_set("nba98", kind ? "JordanActionShot" : "JordanHeadshot", val);
        }
        break;
    }
    }
}

/* ---------------------------------------------------------------- restart pop-up (0.7.1) */
/*
 * Most of our settings only take effect when the game starts. When the player leaves a page
 * (Back, Esc, the back arrow) after changing one of those, and the settings now differ from
 * the ones the game started with, we ask "Restart now to apply the changes?" with the game's
 * own pop-up (the one Game Setup shows on Esc: 0x4E9C48). Yes: the core writes
 * nba98data\restart.req and the game quits the way Game Setup's quit does (0x4DA6A8, then the
 * exit screen 6, skipping the credits), so it saves, restores the desktop and deletes nbatemp.
 * The launcher, which waits for the game, sees the request and starts it again. No: the
 * settings stay saved and apply at the next start. The question is only asked when a launcher
 * is waiting (NBA98_RESTART_OK=1, set by the launcher); otherwise the page notes say it all.
 */
static char g_opt_start[SET_COUNT][64];   /* each restart setting's row text when the game started */
static int g_opt_can_restart, g_opt_restart;

static int opt_needs_restart(int set)
{
    return set != SET_MENU_PLAYER && set != SET_MENU_TINT && set != SET_CRT && set != SET_DEADZONE && set != SET_STICK &&
           set != SET_PADMENUS && set != SET_LOD && set != SET_SKIP_CREDITS;
}

static void opt_restart_snapshot(void)
{
    int i;
    char v[8];
    for (i = 0; i < SET_COUNT; i++) if (opt_needs_restart(i)) opt_setting_text(i, g_opt_start[i], sizeof g_opt_start[i]);
    g_opt_can_restart = GetEnvironmentVariableA("NBA98_RESTART_OK", v, sizeof v) && v[0] == '1';
}

/* do the settings now differ from the ones this game is running with? */
static int opt_restart_pending(void)
{
    char t[64];
    int i;
    for (i = 0; i < SET_COUNT; i++) {
        if (!opt_needs_restart(i)) continue;
        opt_setting_text(i, t, sizeof t);
        if (strcmp(t, g_opt_start[i])) return 1;
    }
    return 0;
}

/* the game's Yes/No pop-up (variadic: every argument on the stack, caller pops); returns its event */
uint32_t opt_dialog_call(const uint32_t *args13, uint32_t eaxv) __asm__("nba98_opt_dialog_call");
__asm__(".text\n"
        "nba98_opt_dialog_call:\n"
        "  pushl %ebx\n  pushl %esi\n  pushl %edi\n  pushl %ebp\n"
        "  movl 20(%esp), %esi\n"
        "  movl 24(%esp), %edi\n"
        "  movl $13, %ecx\n"
        "1:\n"
        "  pushl -4(%esi,%ecx,4)\n"   /* args[12] first, args[0] last (nearest the return address) */
        "  loop 1b\n"
        "  movl %edi, %eax\n"          /* as Game Setup leaves them: EAX the box colour, EDX 0, EBX 1 */
        "  xorl %edx, %edx\n"
        "  movl $1, %ebx\n"
        "  movl $0x4E9C48, %esi\n"
        "  call *%esi\n"
        "  addl $52, %esp\n"
        "  popl %ebp\n  popl %edi\n  popl %esi\n  popl %ebx\n"
        "  ret\n");

static int opt_ask_restart(uint32_t screen)
{
    static const char l1[] = "Restart now to", l2[] = "apply the changes?";
    uint32_t a[13], box, r;
    box = opt_wcall(0x525C5C, 0xFFFF50FF, 0, 0, 0);
    a[0] = 0; a[1] = box; a[2] = 0x140; a[3] = 0xF0;        /* centred: 320, 240 */
    a[4] = a[5] = a[6] = 0;
    a[7] = 0x5C6509;                                         /* font, as Game Setup's quit question */
    a[8] = opt_wcall(0x525C5C, 0xFFFFFFFF, 0, 0, 0);        /* white text */
    a[9] = 0;
    a[10] = 2;                                               /* two lines */
    a[11] = (uint32_t)(uintptr_t)l1; a[12] = (uint32_t)(uintptr_t)l2;
    opt_wcall(0x4C9558, screen, 0, 0, 0x1E0);                /* page buttons off while it is up */
    r = opt_dialog_call(a, box) & 0xFF;
    opt_wcall(0x4C9558, screen, 0, 1, 0x1E0);
    LOG("options menu: restart question answered %s", r == EV_ACCEPT ? "Yes" : "No");
    return r == EV_ACCEPT;
}

/* write the request for the launcher; 0 if it could not be written */
static int opt_write_restart_request(void)
{
    char path[MAX_PATH];
    static const char txt[] = "restart requested from Enhanced Options\r\n";
    _snprintf(path, sizeof path, "%snba98data\\restart.req", g_exe_dir);
    path[sizeof path - 1] = 0;
    return rm_write_all(path, txt, (DWORD)(sizeof txt - 1), NULL, 0, NULL, 0);
}

/* ---------------------------------------------------------------- one page, as a game screen */

static char g_opt_obj[16][16], g_opt_text[16][64];   /* object names and row texts of the open page */

static uint32_t opt_page_open(int p)
{
    char lay[32];
    uint32_t screen, lines;
    const opt_page *pg = &k_pages[p];
    int i;
    _snprintf(lay, sizeof lay, "nbaopt%d.lay", p);
    /* set-up as Rules/Options does it (0x49E748) */
    opt_wcall(0x459D7C, 0x5C6514, 0, 0, 0);
    /* 0x525C5C converts the colour to the screen format and returns it; 0x459FCC sets the text colour from EAX */
    opt_wcall(0x459FCC, opt_wcall(0x525C5C, 0xFFFFFFFF, 0, 0, 0), 0, 0, 0);
    W0(0x455A04);
    W0(0x4531B4);
    opt_wcall(0x44A718, 0, 0, 0, 0);
    screen = opt_wcall(0x4C6100, (uint32_t)(uintptr_t)lay, 0, 0, 0);          /* load the layout */
    if (!screen) { LOG("options menu: page %d layout did not load", p); return 0; }
    lines = opt_wcall(0x495954, 1, 0, 0, 0);
    GD(0x5F388C) = lines;
    opt_wcall(0x4C7B1C, screen, lines, 0, 0);
    opt_wcall(0x4CA22C, (uint32_t)(uintptr_t)"gsetup.lin", GD(0x5F388C), 0, 0);  /* the line drawing and arrows */
    opt_wcall(0x4C39D8, (uint32_t)(uintptr_t)"gsetup.csv", 0, 0, 0);           /* text table (our texts are plain) */
    opt_wcall(0x4C943C, screen, (uint32_t)(uintptr_t)pg->title, 0, 0);          /* banner */
    for (i = 0; i < pg->nitems && i < 16; i++) {
        const char *text = pg->items[i].label;
        _snprintf(g_opt_obj[i], sizeof g_opt_obj[i], "Item%d", i);   /* the game may keep these pointers */
        if (pg->items[i].setting >= 0) {
            opt_setting_text(pg->items[i].setting, g_opt_text[i], sizeof g_opt_text[i]);
            text = g_opt_text[i];
        }
        opt_wcall4(0x4C81C0, screen, (uint32_t)(uintptr_t)g_opt_obj[i], (uint32_t)(uintptr_t)text, OPT_ITEM_ID0 + i, 0, 0, 0, 0);
    }
    /* end of set-up, as every front-end screen does */
    GB(0x5F9113) = 1;
    GB(0x5F9213) = 1;
    opt_wcall(0x51B530, 0x14D, 0x1BD, 0, 0);
    W0(0x48B9E0);
    W0(0x48BAA8);
    return screen;
}

/* run the open page until a button, back or accept; returns the event id */
static int opt_page_run(uint32_t screen, int p)
{
    const opt_page *pg = &k_pages[p];
    int ev, result = -1, changed = 0;
    opt_wcall(0x42F850, 0xFF00, 0xFFFFFFFF, 0x280, 0x1E0);
    W0(0x41D8A0);
    W0(0x401014);
    opt_wcall(0x5224C0, 0, 0, 0x280, 0x1E0);
    opt_wcall(0x522550, GD(0x5F388C), 0, 0, 0x1E0);
    opt_wcall(0x4C9534, screen, 0, 0, 0);
    opt_wcall1(0x4CA9C8, (uint32_t)((int32_t)GD(0x5F9110) >> 24), (uint32_t)((int32_t)GD(0x5F9210) >> 24),
               (uint32_t)((int32_t)GD(0x5F9190) >> 24), 0, 0);
    opt_wcall(0x459D7C, 0x5C6509, 0, 0, 0);
    /* 0x525C5C converts the colour to the screen format and returns it; 0x459FCC sets the text colour from EAX */
    opt_wcall(0x459FCC, opt_wcall(0x525C5C, 0xFFEFDB5F, 0, 0, 0), 0, 0, 0);
    ((ui_label_fn)0x4C716C)(screen, "Heading", 0, 0, pg->heading);   /* yellow, as the main menu's headings */
    if (pg->note) {
        opt_wcall(0x459FCC, opt_wcall(0x525C5C, 0xFFFFFFFF, 0, 0, 0), 0, 0, 0);   /* white */
        ((ui_label_fn)0x4C716C)(screen, "Note", 0, 0, pg->note);
        if (pg->note2) ((ui_label_fn)0x4C716C)(screen, "Note2", 0, 0, pg->note2);
    }
    opt_wcall(0x4576C0, 1, 0x280, 0, 0);
    opt_wcall(0x454ABC, 1, 0, 0, 0);
    W0(0x45F1D0);
    opt_wcall(0x458E14, 0, 0x280, 0, 0x1E0);
    opt_wcall(0x4C9558, screen, 0, 1, 0x1E0);
    opt_wcall(0x51B9C4, GD(0x5F388C), 0, 0, 0);
    while (result < 0) {
        W0(0x48C59C);
        W0(0x45F2A0);
        ev = (int)(opt_wcall(0x4585E4, 0, 0, 0, 0) & 0xFFFF);
        if (GB(0x5C6472)) W0(0x457D3C);
        if (opt_wcall(0x455904, 0, 0, 0, 0) & 0xFFFF) W0(0x454B6C);
        W0(0x42F8FC);
        if (ev && ev != 0xFFFF) LOG("options menu: page %d event 0x%x", p, ev);
        if (ev == EV_BACK || ev == EV_ACCEPT) result = ev;
        else if (ev >= OPT_ITEM_ID0 && ev < OPT_ITEM_ID0 + pg->nitems && pg->items[ev - OPT_ITEM_ID0].setting < 0 &&
                 pg->items[ev - OPT_ITEM_ID0].target < 0) result = ev;   /* the Back button */
        else if (ev >= OPT_ITEM_ID0 && ev < OPT_ITEM_ID0 + pg->nitems && pg->items[ev - OPT_ITEM_ID0].setting >= 0) {
            /* a setting row: next value, saved, then every setting row on the page shows what the ini now holds
               (Resolution's Auto size depends on Display Mode) */
            int i;
            opt_setting_next(pg->items[ev - OPT_ITEM_ID0].setting);
            if (opt_needs_restart(pg->items[ev - OPT_ITEM_ID0].setting)) changed = 1;
            for (i = 0; i < pg->nitems && i < 16; i++) {
                if (pg->items[i].setting < 0) continue;
                opt_setting_text(pg->items[i].setting, g_opt_text[i], sizeof g_opt_text[i]);
                opt_wcall2(0x4C842C, screen, (uint32_t)(uintptr_t)g_opt_obj[i], (uint32_t)(uintptr_t)g_opt_text[i], 0, 0, 0);
            }
        }
        else if (ev >= OPT_ITEM_ID0 && ev < OPT_ITEM_ID0 + pg->nitems) result = ev;
        /* leaving the page after changing a setting that needs a restart: offer it now */
        if (result >= 0 && changed && g_opt_can_restart && opt_restart_pending() && opt_ask_restart(screen))
            g_opt_restart = 1;
        if (!GB(0x5C8286)) break;  /* the game is quitting (0x5C8286 is its "keep running" flag) */
    }
    W0(0x51B9F0);
    opt_wcall(0x4C9558, screen, 0, 0, 0x1E0);
    opt_wcall(0x5224C0, 0, 0, 0x280, 0x1E0);
    opt_wcall(0x522550, GD(0x5F388C), 0, 0, 0x1E0);
    opt_wcall(0x4EBFD8, 1, 0, 0x280, 0x1E0);
    W0(0x4D37A8);
    opt_wcall(0x458E14, 0, 0x280, 0, 0x1E0);
    opt_wcall1(0x4CF668, (uint32_t)((int32_t)GD(0x5F9110) >> 24), (uint32_t)((int32_t)GD(0x5F9210) >> 24),
               (uint32_t)((int32_t)GD(0x5F9190) >> 24), 0, 1);
    opt_wcall(0x4C9514, screen, 0, 0, 0);
    W0(0x45F128);
    return result;
}

static void opt_page_close(uint32_t screen)
{
    opt_wcall(0x4C6C04, screen, 0, 0, 0);
    W0(0x4CA84C);
    W0(0x4C3A28);
    W0(0x455A04);
    W0(0x51B6DC);
    opt_wcall(0x44A718, 0, 0, 0, 0);
}

/* our screen: page stack from the root; returns the screen to go to next (Game Setup) */
uint32_t opt_screen_main(void) __asm__("nba98_opt_screen_main");
uint32_t opt_screen_main(void)
{
    int stack[8], depth = 0;
    stack[0] = 0;
    LOG("options menu: opened");
    while (depth >= 0) {
        int p = stack[depth], ev;
        uint32_t screen = opt_page_open(p);
        if (!screen) break;
        ev = opt_page_run(screen, p);
        opt_page_close(screen);
        if (!GB(0x5C8286)) break;
        if (g_opt_restart) {
            if (opt_write_restart_request()) {
                LOG("options menu: restart: request written, quitting through the game's own quit (saves, then the exit screen)");
                opt_wcall(0x4DA6A8, 0, 3, 0, 0);   /* as Game Setup's quit does before its credits screen */
                return 6;                          /* the exit screen: the game shuts down normally */
            }
            LOG("options menu: restart: the request could not be written; the changes apply at the next start");
            g_opt_restart = 0;
        }
        if (ev >= OPT_ITEM_ID0) {
            int t = k_pages[p].items[ev - OPT_ITEM_ID0].target;
            LOG("options menu: page %d, \"%s\"", p, k_pages[p].items[ev - OPT_ITEM_ID0].label);
            if (t >= 0 && t < OPT_NPAGES && depth < 7) stack[++depth] = t;
            else depth--;
        } else {
            depth--;   /* back arrow, Esc or the forward arrow: up one level */
        }
    }
    LOG("options menu: closed, back to Game Setup");
    return 4;   /* Game Setup */
}

/* ---------------------------------------------------------------- hooks into the exe */

void opt_gsetup_init_thunk(void) __asm__("nba98_opt_gsetup_init_thunk");
void opt_gsetup_event_stub(void) __asm__("nba98_opt_gsetup_event_stub");
void opt_dispatch_stub(void) __asm__("nba98_opt_dispatch_stub");
void opt_add_main_button(uint32_t vars) __asm__("nba98_opt_add_main_button");
void opt_add_main_button(uint32_t vars)
{
    uint32_t screen = vars ? *(uint32_t *)(uintptr_t)vars : 0;
    if (!screen) return;
    opt_wcall4(0x4C81C0, screen, (uint32_t)(uintptr_t)"NBA98Options", (uint32_t)(uintptr_t)"Enhanced Options", OPT_BUTTON_ID,
               0, 0, 0, 0);
}
__asm__(".text\n"
        /* replaces "call 0x49D1E4" (Game Setup init) at 0x4C428E */
        "nba98_opt_gsetup_init_thunk:\n"
        "  movl $0x49D1E4, %eax\n"
        "  call *%eax\n"
        "  pushal\n"
        "  pushl %eax\n"
        "  call nba98_opt_add_main_button\n"
        "  addl $4, %esp\n"
        "  popal\n"
        "  ret\n"
        /* reached from 0x49DD1F (unknown button id in Game Setup), esp = Game Setup's frame */
        "nba98_opt_gsetup_event_stub:\n"
        "  cmpw $0x2F0, %ax\n"
        "  jne 1f\n"
        "  movb $0x1A, 0x128(%esp)\n"
        "  pushl $0x49DBDB\n"
        "  ret\n"
        "1:\n"
        "  pushl $0x49DBD3\n"
        "  ret\n"
        /* screen table entry 0x1A: run our screen, then the game's bookkeeping (as for Rules/Options) */
        "nba98_opt_dispatch_stub:\n"
        "  pushal\n"
        "  call nba98_opt_screen_main\n"
        "  movb %al, nba98_opt_next\n"
        "  popal\n"
        "  movb nba98_opt_next, %ah\n"
        "  movb 0x5C8284, %al\n"
        "  movl 0x5C828A, %edx\n"
        "  movb %al, 0x5C8285\n"
        "  sarl $16, %edx\n"
        "  movb %ah, 0x5C8284\n"
        "  pushl $0x4C412A\n"
        "  ret\n"
        ".data\n"
        "nba98_opt_next: .byte 4\n"
        ".text\n");

static int opt_patch(uint32_t va, const void *bytes, size_t n, const void *expect)
{
    DWORD old;
    void *p = (void *)(uintptr_t)va;
    if (expect && memcmp(p, expect, n)) return 0;
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return 1;
}

static void optmenu_setup(void)
{
    static const unsigned char e_call[5] = {0xE8, 0x51, 0x8F, 0xFD, 0xFF};          /* call 0x49D1E4 at 0x4C428E */
    static const unsigned char e_jne[6] = {0x0F, 0x85, 0xAE, 0xFE, 0xFF, 0xFF};     /* jne 0x49DBD3 at 0x49DD1F */
    static const uint32_t e_tab = 0x4C4130;                                          /* table entry 0x1A: unused */
    static const unsigned char e_look[5] = {0xE8, 0x0D, 0x36, 0x0F, 0x00};           /* call 0x522AF4 at 0x42F4E2 */
    unsigned char look[5];
    unsigned char call[5], jne[6];
    uint32_t tab = (uint32_t)(uintptr_t)opt_dispatch_stub, rel;
    int p;
    if (!ini_int("OptionsMenu", 1)) { LOG("options menu: off (OptionsMenu=0)"); return; }
    if (!g_is_v11) { LOG("options menu: not the verified v1.1 exe; not added"); return; }
    _snprintf(g_opt_dir, sizeof g_opt_dir, "%snba98data", g_exe_dir);
    CreateDirectoryA(g_opt_dir, NULL);
    _snprintf(g_opt_dir, sizeof g_opt_dir, "%snba98data\\menu", g_exe_dir);
    if (!CreateDirectoryA(g_opt_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        LOG("options menu: cannot create %s; not added", g_opt_dir);
        return;
    }
    if (!opt_write_gsetup_lay()) { LOG("options menu: main menu layout not written; not added"); return; }
    g_opt_lines = opt_write_linemap();
    for (p = 0; p < OPT_NPAGES; p++)
        if (!opt_write_page_lay(p)) { LOG("options menu: page %d layout not written; not added", p); return; }
    call[0] = 0xE8;
    rel = (uint32_t)(uintptr_t)opt_gsetup_init_thunk - (0x4C428E + 5);
    memcpy(call + 1, &rel, 4);
    jne[0] = 0x0F; jne[1] = 0x85;
    rel = (uint32_t)(uintptr_t)opt_gsetup_event_stub - (0x49DD1F + 6);
    memcpy(jne + 2, &rel, 4);
    if (!opt_patch(0x4C3E38 + 4 * OPT_SCREEN_ID, &tab, 4, &e_tab) ||
        !opt_patch(0x49DD1F, jne, 6, e_jne) ||
        !opt_patch(0x4C428E, call, 5, e_call)) {
        LOG("options menu: the exe code differs from what was expected; not added");
        return;
    }
    look[0] = 0xE8;
    rel = (uint32_t)(uintptr_t)opt_look_thunk - (0x42F4E2 + 5);
    memcpy(look + 1, &rel, 4);
    if (!opt_patch(0x42F4E2, look, 5, e_look)) LOG("menu look: the exe code differs from what was expected; Menu Player and Colour not pinned");
    opt_restart_snapshot();
    g_opt_on = 1;
    LOG("options menu: restart question %s", g_opt_can_restart ? "on (the launcher is waiting)" : "off (not started by our launcher)");
    LOG("options menu: on (button on Game Setup, screen id 0x%x, %d pages, layouts in %s)", OPT_SCREEN_ID, OPT_NPAGES, g_opt_dir);
}
