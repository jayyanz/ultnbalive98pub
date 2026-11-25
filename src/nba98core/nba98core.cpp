/*
 * nba98core.dll - NBA Live 98 compatibility core (Phase 1, step 1).
 *
 * Loaded into the unmodified NBAWIN.EXE v1.1 by the launcher before any game
 * code runs. Replaces the CD, install-folder and compatibility-shim setup that
 * previously needed a cracked exe, a hand-edited INSTALL.DAT and an installed
 * shim database:
 *
 *  1. CD check: patches the branch at 0x42ED97 in memory (jnz -> nop; jmp), the
 *     same change as the known-good no-CD exe (speech works with it). Applied
 *     only after checking the exe is the v1.1 build we analysed.
 *  2. INSTALL.DAT: the game opens "install.dat" from its working folder. We
 *     redirect that open to a generated copy in nba98data\ whose install
 *     root is the folder the exe is in, so the install can be moved anywhere.
 *     The original file is never modified. "CD" lines are turned into "HD"
 *     lines when the folder exists locally, so nothing is read from a CD.
 *  3. Roster: the 55 players cut before release (art and commentary on the CD,
 *     no database record) are added as free agents, in a copy of the database
 *     under nba98data\ that the game is pointed at (roster_mod.inc).
 *  4. Shims (replacing nba98.sdb):
 *     - GlobalMemoryStatus values are clamped so a modern RAM size cannot
 *       overflow the game's arithmetic (GlobalMemoryStatusLie).
 *     - Single core: only the game's own threads are pinned to one CPU (main
 *       thread, every thread the game creates, and the multimedia timer thread
 *       that runs the game's timer callback). Everything else (GPU driver,
 *       audio, wrappers) uses all CPUs. Pinning the whole process caused the
 *       2.5 s in-game freezes (see project notes, run 8).
 *
 * Settings: nba98.ini next to the exe (all optional).
 * Log: nba98core.log next to the exe.
 */
#define WIN32_LEAN_AND_MEAN
#include "nba98_cxx.h"   /* C++ build settings, first (0.7.15) */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "nba98_build.h"

NBA98_C_BEGIN
/* ------------------------------------------------------------------------- */
/* logging                                                                   */
/* ------------------------------------------------------------------------- */
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static LARGE_INTEGER g_qpf, g_qpc0;
static char g_exe_dir[MAX_PATH];      /* with trailing backslash */
static char g_ini[MAX_PATH];

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_qpc0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
}

static void logf_(const char *fmt, ...)
{
    char buf[1200];
    int n;
    va_list ap;
    DWORD w;
    if (g_log == INVALID_HANDLE_VALUE) return;
    n = _snprintf(buf, 32, "[%9.1f ms] ", now_ms());
    va_start(ap, fmt);
    n += _vsnprintf(buf + n, sizeof buf - n - 3, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    EnterCriticalSection(&g_cs);
    WriteFile(g_log, buf, (DWORD)n, &w, NULL);
    LeaveCriticalSection(&g_cs);
}
#define LOG logf_

static int ini_int(const char *key, int def)
{
    return (int)GetPrivateProfileIntA("nba98", key, def, g_ini);
}

/* ------------------------------------------------------------------------- */
/* IAT hooking on the game exe                                               */
/* ------------------------------------------------------------------------- */
static void *iat_hook(HMODULE mod, const char *dll, const char *fn, void *repl)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!dd->VirtualAddress) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *addrs;
        if (_stricmp((char *)(base + imp->Name), dll)) continue;
        names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            DWORD old;
            void *orig;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((char *)ibn->Name, fn)) continue;
            orig = (void *)(uintptr_t)addrs->u1.Function;
            if (!VirtualProtect(&addrs->u1.Function, 4, PAGE_EXECUTE_READWRITE, &old)) return NULL;
            addrs->u1.Function = (DWORD)(uintptr_t)repl;
            VirtualProtect(&addrs->u1.Function, 4, old, &old);
            return orig;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------------- */
/* exe identification and the CD check patch                                 */
/* ------------------------------------------------------------------------- */
#define EXE_TIMESTAMP_V11 0x346B890A   /* NBAWIN.EXE v1.1, linked 1997-11-13 23:11:06 UTC */
#define CD_BRANCH_VA      0x0042ED97
static int g_is_v11;

static int identify_exe(void)
{
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    const char *msg = (const char *)0x005A1148;
    if ((uintptr_t)base != 0x400000) return 0;
    if (nt->FileHeader.TimeDateStamp != EXE_TIMESTAMP_V11) {
        LOG("exe timestamp 0x%08lx is not NBAWIN.EXE v1.1 (0x%08lx): no code patches applied",
            (unsigned long)nt->FileHeader.TimeDateStamp, (unsigned long)EXE_TIMESTAMP_V11);
        return 0;
    }
    if (memcmp(msg, "Please insert your NBA Live 98 CD", 33)) {
        LOG("exe v1.1 timestamp matches but CD message not found at 0x5A1148: no code patches applied");
        return 0;
    }
    return 1;
}

static void patch_cd_check(void)
{
    BYTE *p = (BYTE *)CD_BRANCH_VA;
    DWORD old;
    if (!ini_int("NoCDCheck", 1)) { LOG("CD check: left as is (NoCDCheck=0)"); return; }
    if (p[0] == 0x90 && p[1] == 0xE9) { LOG("CD check: exe is already patched (no-CD exe)"); return; }
    if (p[0] != 0x0F || p[1] != 0x85) {
        LOG("CD check: unexpected bytes %02X %02X at 0x%08X, not patched", p[0], p[1], CD_BRANCH_VA);
        return;
    }
    /* jnz rel32 (0F 85 xx xx xx xx) -> nop; jmp rel32 (90 E9 xx xx xx xx):
     * the jmp starts one byte later, so its rel32 is the same displacement. */
    if (VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old)) {
        p[0] = 0x90;
        p[1] = 0xE9;
        VirtualProtect(p, 2, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 2);
        LOG("CD check: patched at 0x%08X (jnz -> nop; jmp)", CD_BRANCH_VA);
    }
}

/* ------------------------------------------------------------------------- */
/* INSTALL.DAT redirection                                                   */
/* ------------------------------------------------------------------------- */
typedef HANDLE (WINAPI *CreateFileA_fn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_fn o_CreateFileA;
static char g_install_dat[MAX_PATH];   /* generated copy */
static int g_install_ready;

#include "roster_mod.inc"        /* cut players as free agents (nba98data\ copy of the database) */
#include "jordan_art.inc"        /* optional personal art pack mj.dat (never shipped) */
#include "allstar_fix.inc"       /* 1997 All-Star clubs for the All-Star jerseys (0.7.9) */
#include "optmenu.inc"           /* our settings screens in the front end */
#include "skip_screens.inc"      /* skip the intros and the exit credits (0.7.12) */

/* Default table (from the retail install) used if the folder has no INSTALL.DAT. */
static const char *const k_default_dirs[][2] = {
    {"febgs", "art\\frontend\\backgrnd\\febgs"}, {"bodypart", "art\\frontend\\backgrnd\\bodypart"},
    {"cities", "art\\frontend\\backgrnd\\cities"}, {"playoff", "art\\frontend\\backgrnd\\playoff"},
    {"ports", "art\\frontend\\foregrnd\\ports"}, {"action", "art\\frontend\\foregrnd\\action"},
    {"useraction", "art\\frontend\\foregrnd\\action"}, {"userports", "art\\frontend\\foregrnd\\ports"},
    {"flogos", "art\\frontend\\foregrnd\\logos\\large"}, {"nlogos", "art\\frontend\\foregrnd\\logos\\neon"},
    {"rlogos", "art\\frontend\\foregrnd\\logos\\sphere"}, {"ovllogos", "art\\frontend\\foregrnd\\logos\\overlay"},
    {"userlogof", "art\\frontend\\foregrnd\\logos\\large"}, {"userlogor", "art\\frontend\\foregrnd\\logos\\sphere"},
    {"userlogon", "art\\frontend\\foregrnd\\logos\\neon"}, {"icons", "art\\frontend\\foregrnd\\icons"},
    {"popup", "art\\frontend\\foregrnd\\popup"}, {"boxes", "art\\frontend\\foregrnd\\boxes"},
    {"buttons", "art\\frontend\\foregrnd\\buttons"}, {"fonts", "art\\frontend\\foregrnd\\fonts"},
    {"textart", "art\\frontend\\foregrnd\\etextart"}, {"miscfe", "art\\frontend\\foregrnd\\misc"},
    {"feanims", "art\\frontend\\foregrnd\\anims"}, {"credits", "art\\frontend\\foregrnd\\credits"},
    {"sound", "sound"}, {"songs", "songs"}, {"gamespch", "speech\\gamespch"},
    {"netspch", "speech\\netspch"}, {"playspch", "speech\\playspch"}, {"movies", "movies"},
    {"miscgame", "art\\ingame\\misc"}, {"anims", "art\\ingame\\anims"}, {"plrdata", "art\\ingame\\players"},
    {"teamdata", "art\\ingame\\teams"}, {"linemapviv", ":art\\frontend\\foregrnd\\etextart\\linemap.viv"},
    {"dbdata", "dbdata"}, {"origdb", "origdb"}, {"saves", "saves"}, {"csv", "csv"},
    {"placehold", "art\\frontend\\foregrnd\\plachold"}, {"layouts", "art\\frontend\\layout\\layoute"},
};

static int dir_exists(const char *rel)
{
    char p[MAX_PATH];
    DWORD a;
    _snprintf(p, sizeof p, "%s%s", g_exe_dir, rel[0] == ':' ? rel + 1 : rel);
    p[sizeof p - 1] = 0;
    a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES;
}

static void build_install_dat(void)
{
    char src[MAX_PATH], tmpdir[MAX_PATH], root[MAX_PATH], *text = NULL, *out;
    HANDLE f;
    DWORD size = 0, rd = 0, w;
    size_t cap, len = 0;
    int i, lines = 0, converted = 0, redirected = 0, after_end = 0, tail_line = 0;
    char *line, *next;

    /* install root = exe folder without the trailing backslash (matches retail INSTALL.DAT) */
    strcpy(root, g_exe_dir);
    if (strlen(root) > 3 && root[strlen(root) - 1] == '\\') root[strlen(root) - 1] = 0;

    _snprintf(src, sizeof src, "%sINSTALL.DAT", g_exe_dir);
    f = o_CreateFileA(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        size = GetFileSize(f, NULL);
        text = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
        if (text) ReadFile(f, text, size, &rd, NULL);
        CloseHandle(f);
    }
    cap = (size_t)size + 8192;
    out = (char *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, cap);
    if (!out) return;

#define EMIT(...) do { int _n = _snprintf(out + len, cap - len, __VA_ARGS__); if (_n > 0) len += (size_t)_n; } while (0)
    if (text && rd) {
        for (line = text; line && *line; line = next) {
            char name[64] = "", path[MAX_PATH] = "", type[4] = "";
            next = strchr(line, '\n');
            if (next) *next++ = 0;
            if (strchr(line, '\r')) *strchr(line, '\r') = 0;
            if (!after_end) {
                if (!_strnicmp(line, "END", 3)) { EMIT("END\r\n"); after_end = 1; continue; }
                if (sscanf(line, "%3s %63s %259s", type, name, path) == 3 &&
                    (!_stricmp(type, "HD") || !_stricmp(type, "CD"))) {
                    /* keep the line byte for byte (the parser expects the retail fixed-width
                     * columns); only turn a CD line into an HD line if the folder is local */
                    if (!_stricmp(type, "CD") && dir_exists(path)) { line[0] = 'H'; line[1] = 'D'; converted++; }
                    if (roster_redirect(name)) {
                        /* same line up to the path column, then our folder */
                        char *pp = strstr(line + 3 + strlen(name), path);
                        if (pp) { *pp = 0; EMIT("%s%s\r\n", line, roster_redirect(name)); redirected++; }
                        else EMIT("%s\r\n", line);
                    } else
                        EMIT("%s\r\n", line);
                    lines++;
                } else {
                    EMIT("%s\r\n", line);
                }
            } else {
                /* first line after END is the quoted install root; keep the rest (size, detail, language) */
                if (tail_line++ == 0) EMIT("\"%s\"\r\n", root);
                else EMIT("%s\r\n", line);
            }
        }
        if (!after_end) { EMIT("END\r\n\"%s\"\r\nCUSTOM\r\nHIGH\r\nENGLISH\r\n", root); }
        LOG("INSTALL.DAT: read %s (%lu bytes, %d folder lines, %d CD lines made local, %d redirected to nba98data)",
            src, (unsigned long)rd, lines, converted, redirected);
    } else {
        for (i = 0; i < (int)(sizeof k_default_dirs / sizeof k_default_dirs[0]); i++)
            EMIT("HD %-23s%s\r\n", k_default_dirs[i][0],
                 roster_redirect(k_default_dirs[i][0]) ? roster_redirect(k_default_dirs[i][0]) : k_default_dirs[i][1]);   /* retail layout: path at column 27 */
        EMIT("END\r\n\"%s\"\r\nCUSTOM\r\nHIGH\r\nENGLISH\r\n", root);
        LOG("INSTALL.DAT: none in %s, generated the retail default table", g_exe_dir);
    }
#undef EMIT

    /* 0.7.20: the copy lives in <install>\nba98data\ (was %TEMP%\NBALive98\), so nothing
     * is written outside the game folder. The old %TEMP% copy is removed if one is left. */
    GetTempPathA(sizeof tmpdir, tmpdir);
    strncat(tmpdir, "NBALive98\\INSTALL.DAT", sizeof tmpdir - strlen(tmpdir) - 1);
    if (DeleteFileA(tmpdir)) {
        tmpdir[strlen(tmpdir) - 12] = 0;
        RemoveDirectoryA(tmpdir);
        LOG("INSTALL.DAT: removed the old copy in %s", tmpdir);
    }
    _snprintf(tmpdir, sizeof tmpdir, "%snba98data", g_exe_dir);
    tmpdir[sizeof tmpdir - 1] = 0;
    CreateDirectoryA(tmpdir, NULL);
    _snprintf(g_install_dat, sizeof g_install_dat, "%s\\INSTALL.DAT", tmpdir);
    f = o_CreateFileA(g_install_dat, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, out, (DWORD)len, &w, NULL);
        CloseHandle(f);
        g_install_ready = 1;
        LOG("INSTALL.DAT: install root set to \"%s\"; game reads %s", root, g_install_dat);
    } else {
        LOG("INSTALL.DAT: could not write %s (error %lu); game will use the original", g_install_dat, GetLastError());
    }
    if (text) HeapFree(GetProcessHeap(), 0, text);
    HeapFree(GetProcessHeap(), 0, out);
}

static int is_install_dat(const char *path)
{
    const char *b1 = strrchr(path, '\\'), *b2 = strrchr(path, '/'), *b = path;
    if (b1 && b1 + 1 > b) b = b1 + 1;
    if (b2 && b2 + 1 > b) b = b2 + 1;
    return _stricmp(b, "install.dat") == 0;
}

#include "tempbuf.inc"
#include "spchmem.inc"         /* speech banks kept in memory (0.7.11) */

static void (*g_prof_open)(const char *name, double ms);   /* Debug stall profiler (profile.inc) */
static volatile int g_menus_seen;   /* 0.7.12: the game has opened a menu layout (*.lay): the intros are over */

static HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                   DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (name && g_install_ready && is_install_dat(name)) {
        if (access & GENERIC_WRITE) {
            /* the game rewriting its install info: let it write our copy, never the original */
            LOG("INSTALL.DAT: game opened %s for writing; redirected to %s", name, g_install_dat);
        } else {
            LOG("INSTALL.DAT: game opened %s; redirected to %s", name, g_install_dat);
        }
        return o_CreateFileA(g_install_dat, access, share, sa, disp, flags, tmpl);
    }
    if (!g_menus_seen && name) {
        size_t l = strlen(name);
        if (l > 4 && !_stricmp(name + l - 4, ".lay")) g_menus_seen = 1;
    }
    static int log_opens = -1;   /* read once: GetPrivateProfileInt reads the ini file every call */
    if (log_opens < 0) log_opens = NBA98_DEBUG ? ini_int("LogFileOpens", 0) : 0;
    if (log_opens && name)
        LOG("open %s (access 0x%lx, disposition %lu)", name, access, disp);
    {
        char art[MAX_PATH];
        if (g_opt_on && opt_redirect(name, art, sizeof art)) {
            if (log_opens) LOG("open %s: options menu, redirected to %s", name, art);
            return o_CreateFileA(art, access, share, sa, disp, flags, tmpl);
        }
        if ((g_art_on || g_spch_on) && art_redirect(name, access, art, sizeof art)) {
            HANDLE m;
            if (log_opens) LOG("open %s: Jordan art, redirected to %s", name, art);
            if ((m = spm_try_open(art, access, disp, flags)) != NULL) return m;
            return o_CreateFileA(art, access, share, sa, disp, flags, tmpl);
        }
    }
    {
        HANDLE m = spm_try_open(name, access, disp, flags);
        if (m) {
            if (log_opens) LOG("open %s: speech bank in memory", name);
            if (g_prof_open && name) g_prof_open(name, 0.0);
            return m;
        }
    }
    if (g_tmb_on) tmb_before_open(name);
    if (g_prof_open && name) {
        LARGE_INTEGER a, b;
        HANDLE h;
        QueryPerformanceCounter(&a);
        h = o_CreateFileA(name, access, share, sa, disp, flags, tmpl);
        QueryPerformanceCounter(&b);
        g_prof_open(name, (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)g_qpf.QuadPart);
        if (g_tmb_on) tmb_opened(h, name, access, disp, flags);
        return h;
    }
    {
        HANDLE h = o_CreateFileA(name, access, share, sa, disp, flags, tmpl);
        if (g_tmb_on) tmb_opened(h, name, access, disp, flags);
        return h;
    }
}

/* ------------------------------------------------------------------------- */
/* GlobalMemoryStatus clamp (replaces the GlobalMemoryStatusLie shim)        */
/* ------------------------------------------------------------------------- */
typedef void (WINAPI *GlobalMemoryStatus_fn)(LPMEMORYSTATUS);
static GlobalMemoryStatus_fn o_GlobalMemoryStatus;
#define MEM_CAP 0x20000000u   /* 512 MB: far more than the game needs, safe in signed 32-bit maths */

static void WINAPI h_GlobalMemoryStatus(LPMEMORYSTATUS ms)
{
    static LONG logged;
    o_GlobalMemoryStatus(ms);
    if (ms->dwTotalPhys > MEM_CAP) ms->dwTotalPhys = MEM_CAP;
    if (ms->dwAvailPhys > MEM_CAP) ms->dwAvailPhys = MEM_CAP;
    if (ms->dwTotalPageFile > MEM_CAP) ms->dwTotalPageFile = MEM_CAP;
    if (ms->dwAvailPageFile > MEM_CAP) ms->dwAvailPageFile = MEM_CAP;
    if (ms->dwTotalVirtual > MEM_CAP) ms->dwTotalVirtual = MEM_CAP;
    if (ms->dwAvailVirtual > MEM_CAP) ms->dwAvailVirtual = MEM_CAP;
    if (!InterlockedExchange(&logged, 1))
        LOG("GlobalMemoryStatus: reporting at most 512 MB to the game");
}

/* ------------------------------------------------------------------------- */
/* Game-thread CPU pinning (replaces SingleProcAffinity)                     */
/* ------------------------------------------------------------------------- */
static DWORD_PTR g_game_mask;
static int g_pin;

/* ------------------------------------------------------------------------- */
/* TerminateThread. The game kills its streaming worker (entry 0x5340AC,     */
/* helper 0x5383C0) whenever a movie or stream is stopped. Killing a thread  */
/* while it holds a lock leaves that lock taken for ever, and the game then  */
/* freezes (seen under Wine at startup and when skipping the intro; the same */
/* can happen on Windows if the thread holds a heap or game lock).           */
/* With SafeTerminate=1 (default) the kill waits for a safe point:           */
/*  - the thread holds none of the game's critical sections (the exe's       */
/*    EnterCriticalSection/LeaveCriticalSection are counted per thread), and */
/*  - it is either blocked in one of the exe's waits (WaitForSingleObject,   */
/*    WaitForMultipleObjectsEx, SleepEx): terminated there, or running the   */
/*    game's own code: sent to ExitThread from that point.                   */
/* It retries for up to 2 s, then falls back to a plain TerminateThread.     */
/* ------------------------------------------------------------------------- */
typedef BOOL (WINAPI *TerminateThread_fn)(HANDLE, DWORD);
typedef void (WINAPI *CS_fn)(LPCRITICAL_SECTION);
typedef DWORD (WINAPI *WaitSingle_fn)(HANDLE, DWORD);
typedef DWORD (WINAPI *WaitMultiEx_fn)(DWORD, const HANDLE *, BOOL, DWORD, BOOL);
typedef DWORD (WINAPI *SleepEx_fn)(DWORD, BOOL);
static TerminateThread_fn o_TerminateThread;
static CS_fn o_EnterCS, o_LeaveCS;
static WaitSingle_fn o_WaitSingle;
static WaitMultiEx_fn o_WaitMultiEx;
static SleepEx_fn o_SleepEx;
static int g_safe_terminate;

#define TS_MAX 128
static struct { volatile DWORD tid; volatile LONG held, waiting; } g_ts[TS_MAX];

static int ts_slot(DWORD tid, int create)
{
    int i;
    for (i = 0; i < TS_MAX; i++) if (g_ts[i].tid == tid) return i;
    if (!create) return -1;
    for (i = 0; i < TS_MAX; i++)
        if (InterlockedCompareExchange((volatile LONG *)&g_ts[i].tid, (LONG)tid, 0) == 0) {
            g_ts[i].held = 0; g_ts[i].waiting = 0;
            return i;
        }
    return -1;
}
static void WINAPI h_EnterCS(LPCRITICAL_SECTION cs)
{
    int i;
    o_EnterCS(cs);
    i = ts_slot(GetCurrentThreadId(), 1);
    if (i >= 0) g_ts[i].held++;
}
static void WINAPI h_LeaveCS(LPCRITICAL_SECTION cs)
{
    int i = ts_slot(GetCurrentThreadId(), 1);
    if (i >= 0) g_ts[i].held--;
    o_LeaveCS(cs);
}
static DWORD WINAPI h_WaitSingle(HANDLE h, DWORD ms)
{
    DWORD r;
    int i = ts_slot(GetCurrentThreadId(), 1);
    if (i >= 0) g_ts[i].waiting++;
    r = o_WaitSingle(h, ms);
    if (i >= 0) g_ts[i].waiting--;
    return r;
}
static DWORD WINAPI h_WaitMultiEx(DWORD n, const HANDLE *hs, BOOL all, DWORD ms, BOOL alertable)
{
    DWORD r;
    int i = ts_slot(GetCurrentThreadId(), 1);
    if (i >= 0) g_ts[i].waiting++;
    r = o_WaitMultiEx(n, hs, all, ms, alertable);
    if (i >= 0) g_ts[i].waiting--;
    return r;
}
static DWORD WINAPI h_SleepEx(DWORD ms, BOOL alertable)
{
    DWORD r;
    int i = ts_slot(GetCurrentThreadId(), 1);
    if (i >= 0) g_ts[i].waiting++;
    r = o_SleepEx(ms, alertable);
    if (i >= 0) g_ts[i].waiting--;
    return r;
}

static int in_exe(uint32_t a)
{
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((BYTE *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    return a >= (uint32_t)(uintptr_t)exe && a < (uint32_t)(uintptr_t)exe + nt->OptionalHeader.SizeOfImage;
}

static BOOL WINAPI h_TerminateThread(HANDLE h, DWORD code)
{
    DWORD tid = GetThreadId(h);
    double t0 = now_ms();
    int tries, slot;
    if (!g_safe_terminate || !tid || tid == GetCurrentThreadId()) {
        LOG("TerminateThread(tid %lu)", tid);
        return o_TerminateThread(h, code);
    }
    slot = ts_slot(tid, 0);
    for (tries = 0; tries < 2000; tries++) {
        CONTEXT c;
        LONG held, waiting;
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) {
            LOG("TerminateThread(tid %lu): the thread had already ended", tid);
            return TRUE;
        }
        if (SuspendThread(h) == (DWORD)-1) {
            if (WaitForSingleObject(h, 100) == WAIT_OBJECT_0) {
                LOG("TerminateThread(tid %lu): the thread ended by itself", tid);
                return TRUE;
            }
            break;
        }
        held = slot >= 0 ? g_ts[slot].held : 0;
        waiting = slot >= 0 ? g_ts[slot].waiting : 0;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        if (held <= 0 && (DWORD)(uintptr_t)g_cs.OwningThread != tid && GetThreadContext(h, &c)) {
            if (waiting > 0) {
                BOOL r = o_TerminateThread(h, code);
                ResumeThread(h);
                LOG("TerminateThread(tid %lu): terminated while waiting, after %.1f ms", tid, now_ms() - t0);
                return r;
            }
            if (in_exe(c.Eip)) {
                uint32_t *sp = (uint32_t *)(uintptr_t)(c.Esp - 8);
                sp[0] = 0;          /* return address (ExitThread never returns) */
                sp[1] = code;
                c.Esp = (DWORD)(uintptr_t)sp;
                c.Eip = (DWORD)(uintptr_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "ExitThread");
                if (SetThreadContext(h, &c)) {
                    ResumeThread(h);
                    WaitForSingleObject(h, 1000);
                    LOG("TerminateThread(tid %lu): sent to ExitThread from game code after %.1f ms", tid, now_ms() - t0);
                    return TRUE;
                }
            }
        }
        ResumeThread(h);
        Sleep(1);
    }
    LOG("TerminateThread(tid %lu): no safe point found in %.0f ms, terminating anyway", tid, now_ms() - t0);
    return o_TerminateThread(h, code);
}

typedef HANDLE (WINAPI *CreateThread_fn)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static CreateThread_fn o_CreateThread;
static volatile LONG g_nknown;
static DWORD g_known_tid[64];
#if NBA98_DEBUG
static volatile double g_bw_joy_ms, g_bw_snd_ms;   /* boot watch (diag.inc) */
#endif
static void note_thread(DWORD tid) { LONG i = InterlockedIncrement(&g_nknown) - 1; if (i < 64) g_known_tid[i] = tid; }

/* Joystick thread start race (the boot pause, 7 to 25 s on Jason's PC).
   The game (0x4f33c8) creates its joystick thread (0x4f3174, above normal
   priority) and only afterwards creates the event (0x6247fc) and lock
   (0x624800) the thread uses. If the thread runs first, its wait on the
   missing event fails at once and it polls the joysticks in a tight loop.
   With the game's threads on one CPU it then starves the main thread, which
   needs the CPU to create that event: Windows only lets the main thread in
   now and then (its starvation boost, about every 4 s). The original game
   got away with it on machines where the main thread kept running.
   Fix: the joystick thread first waits (sleeping, so the main thread runs)
   until the game has set both, at most 2 s. */
#define JOY_EVENT ((volatile uint32_t *)0x6247fc)
#define JOY_LOCK  ((volatile uint32_t *)0x624800)
typedef struct { LPTHREAD_START_ROUTINE fn; LPVOID arg; uint32_t old_ev, old_lock; } joy_start;
static DWORD WINAPI joy_thread_wrap(LPVOID p)
{
    joy_start js = *(joy_start *)p;
    double t0 = now_ms();
    int n = 0;
    HeapFree(GetProcessHeap(), 0, p);
    while ((*JOY_EVENT == js.old_ev || !*JOY_EVENT || *JOY_LOCK == js.old_lock || !*JOY_LOCK) && now_ms() - t0 < 2000.0) {
        Sleep(1);
        n++;
    }
    LOG("joystick thread start: game's event and lock ready after %.1f ms%s", now_ms() - t0,
        n ? " (waited, so the joystick thread could not lock out the main thread)" : "");
    return js.fn(js.arg);
}

static HANDLE WINAPI h_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T ss, LPTHREAD_START_ROUTINE fn,
                                    LPVOID arg, DWORD flags, LPDWORD tid_out)
{
    DWORD tid = 0;
    HANDLE h;
    if ((uintptr_t)fn == 0x53827c && (uintptr_t)arg == 0x4f3174 && g_is_v11) {
        joy_start *js = (joy_start *)HeapAlloc(GetProcessHeap(), 0, sizeof *js);
        if (js) {
            js->fn = fn; js->arg = arg; js->old_ev = *JOY_EVENT; js->old_lock = *JOY_LOCK;
            h = o_CreateThread(sa, ss, joy_thread_wrap, js, flags, &tid);
            if (!h) HeapFree(GetProcessHeap(), 0, js);
            goto created;
        }
    }
    h = o_CreateThread(sa, ss, fn, arg, flags, &tid);
created:
    if (tid_out) *tid_out = tid;
    if (tid) note_thread(tid);
    if (h && g_pin) SetThreadAffinityMask(h, g_game_mask);
#if NBA98_DEBUG
    if ((uintptr_t)arg == 0x4f3174 && !g_bw_joy_ms) g_bw_joy_ms = now_ms();
    if ((uintptr_t)arg == 0x551b4c && !g_bw_snd_ms) g_bw_snd_ms = now_ms();
#endif
    LOG("game thread created: tid %lu, entry 0x%08lx (arg 0x%08lx)%s", tid,
        (unsigned long)(uintptr_t)fn, (unsigned long)(uintptr_t)arg, g_pin ? ", pinned" : "");
    return h;
}

#include "fscache.inc"
#include "sysinfo.inc"
#include "timing.inc"

/* timeSetEvent: wrap the callback so the multimedia timer thread that runs the
 * game's code gets pinned too, the first time it calls us. */
typedef MMRESULT (WINAPI *timeSetEvent_fn)(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT);
static timeSetEvent_fn o_timeSetEvent;
#define MAX_TIMERS 16
static struct { LPTIMECALLBACK cb; DWORD_PTR user; } g_timers[MAX_TIMERS];
static LONG g_ntimers;
static DWORD g_pinned_timer_tid[8];
static LONG g_npinned_timer;

static void CALLBACK timer_thunk(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR d1, DWORD_PTR d2)
{
    LONG slot = (LONG)user;
    DWORD me = GetCurrentThreadId();
    LONG i, n = g_npinned_timer;
    int known = 0;
    for (i = 0; i < n && i < 8; i++) if (g_pinned_timer_tid[i] == me) { known = 1; break; }
    if (!known && g_pin) {
        SetThreadAffinityMask(GetCurrentThread(), g_game_mask);
        i = InterlockedIncrement(&g_npinned_timer) - 1;
        if (i < 8) g_pinned_timer_tid[i] = me;
        note_thread(me);
        LOG("timer thread %lu pinned (runs game timer callback 0x%08lx)", me,
            (unsigned long)(uintptr_t)g_timers[slot].cb);
    }
    timing_on_timer(g_timers[slot].cb);
    g_timers[slot].cb(id, msg, g_timers[slot].user, d1, d2);
    timing_after_timer(g_timers[slot].cb);
}

static MMRESULT WINAPI h_timeSetEvent(UINT delay, UINT res, LPTIMECALLBACK cb, DWORD_PTR user, UINT flags)
{
    LONG slot, n;
    /* TIME_CALLBACK_EVENT_SET/PULSE pass an event handle, not a function */
    if (flags & (TIME_CALLBACK_EVENT_SET | TIME_CALLBACK_EVENT_PULSE) || !cb)
        return o_timeSetEvent(delay, res, cb, user, flags);
    /* the game re-arms a one-shot timer from inside its callback every few ms:
     * reuse the slot for the same callback and user value */
    n = g_ntimers;
    for (slot = 0; slot < n; slot++)
        if (g_timers[slot].cb == cb && g_timers[slot].user == user) break;
    if (slot == n) {
        EnterCriticalSection(&g_cs);
        n = g_ntimers;
        for (slot = 0; slot < n; slot++)
            if (g_timers[slot].cb == cb && g_timers[slot].user == user) break;
        if (slot == n) {
            if (n >= MAX_TIMERS) {
                LeaveCriticalSection(&g_cs);
                return o_timeSetEvent(delay, res, cb, user, flags);
            }
            g_timers[n].cb = cb;
            g_timers[n].user = user;
            InterlockedExchange(&g_ntimers, n + 1);
            LOG("timeSetEvent: new timer callback 0x%08lx (user 0x%08lx), first delay %u ms, flags 0x%x",
                (unsigned long)(uintptr_t)cb, (unsigned long)user, delay, flags);
        }
        LeaveCriticalSection(&g_cs);
    }
    return o_timeSetEvent(delay, res, timer_thunk, (DWORD_PTR)slot, flags);
}

/* ------------------------------------------------------------------------- */
/* DirectDraw: keep the desktop display mode (3dfx mode only).               */
/* Even with Glide the game creates a DirectDraw object, takes exclusive     */
/* full-screen control and switches the monitor to 640x480 (wrapper code     */
/* around 0x5553C0). dgVoodoo used to absorb that; now our glide2x.dll draws */
/* to a desktop-sized window, so with KeepDesktopMode=1 (default) the        */
/* mode change is skipped and the cooperative level made normal. The game's  */
/* DirectDraw interfaces are patched in place (SetCooperativeLevel,          */
/* SetDisplayMode, RestoreDisplayMode), including IDirectDraw2/4/7 obtained  */
/* through QueryInterface.                                                   */
/* ------------------------------------------------------------------------- */
typedef HRESULT (WINAPI *DDCreate_fn)(GUID *, void **, void *);
typedef HRESULT (WINAPI *DDQI_fn)(void *, const GUID *, void **);
typedef HRESULT (WINAPI *DDCoop_fn)(void *, HWND, DWORD);
typedef HRESULT (WINAPI *DDMode1_fn)(void *, DWORD, DWORD, DWORD);
typedef HRESULT (WINAPI *DDMode2_fn)(void *, DWORD, DWORD, DWORD, DWORD, DWORD);
typedef HRESULT (WINAPI *DDRestore_fn)(void *);
static DDCreate_fn o_DirectDrawCreate;
static int g_keep_desktop;

#define DD_MAXVT 8
static struct { void **vt; int v2; DDQI_fn qi; DDCoop_fn coop; void *mode; DDRestore_fn restore; } g_ddvt[DD_MAXVT];
static int g_nddvt;

static int dd_find(void *obj)
{
    void **vt = *(void ***)obj;
    int i;
    for (i = 0; i < g_nddvt; i++) if (g_ddvt[i].vt == vt) return i;
    return -1;
}
static void dd_patch(void *obj, int v2);

static HRESULT WINAPI h_dd_qi(void *self, const GUID *iid, void **out)
{
    int i = dd_find(self);
    HRESULT hr = i >= 0 ? g_ddvt[i].qi(self, iid, out) : E_NOINTERFACE;
    if (SUCCEEDED(hr) && out && *out) {
        /* IDirectDraw = 6C14DB80, IDirectDraw2 = B3A6F3E0, IDirectDraw4 = 9C59509A, IDirectDraw7 = 15E65EC0 */
        DWORD d1 = iid->Data1;
        if (d1 == 0x6C14DB80) dd_patch(*out, 0);
        else if (d1 == 0xB3A6F3E0 || d1 == 0x9C59509A || d1 == 0x15E65EC0) dd_patch(*out, 1);
    }
    return hr;
}
static HRESULT WINAPI h_dd_coop(void *self, HWND hwnd, DWORD flags)
{
    int i = dd_find(self);
    DWORD nf = (flags & ~(DWORD)(0x1 | 0x10 | 0x40)) | 0x8;  /* drop FULLSCREEN, EXCLUSIVE, ALLOWMODEX; NORMAL */
    LOG("DirectDraw SetCooperativeLevel(0x%lx) -> 0x%lx (desktop mode kept)", flags, nf);
    return i >= 0 ? g_ddvt[i].coop(self, hwnd, nf) : E_FAIL;
}
static HRESULT WINAPI h_dd_mode1(void *self, DWORD w, DWORD h, DWORD bpp)
{
    (void)self;
    LOG("DirectDraw SetDisplayMode(%lux%lu, %lu bit) skipped: desktop mode kept", w, h, bpp);
    return 0;
}
static HRESULT WINAPI h_dd_mode2(void *self, DWORD w, DWORD h, DWORD bpp, DWORD hz, DWORD fl)
{
    (void)self; (void)fl;
    LOG("DirectDraw SetDisplayMode(%lux%lu, %lu bit, %lu Hz) skipped: desktop mode kept", w, h, bpp, hz);
    return 0;
}
static HRESULT WINAPI h_dd_restore(void *self) { (void)self; return 0; }

static void dd_patch(void *obj, int v2)
{
    void **vt = *(void ***)obj;
    DWORD old;
    int i;
    if (dd_find(obj) >= 0 || g_nddvt >= DD_MAXVT) return;
    i = g_nddvt;
    g_ddvt[i].vt = vt;
    g_ddvt[i].v2 = v2;
    g_ddvt[i].qi = (DDQI_fn)vt[0];
    g_ddvt[i].restore = (DDRestore_fn)vt[19];
    g_ddvt[i].coop = (DDCoop_fn)vt[20];
    g_ddvt[i].mode = vt[21];
    if (!VirtualProtect(vt, 22 * sizeof(void *), PAGE_READWRITE, &old)) { LOG("DirectDraw: cannot patch the interface"); return; }
    vt[0] = (void *)h_dd_qi;
    vt[19] = (void *)h_dd_restore;
    vt[20] = (void *)h_dd_coop;
    vt[21] = v2 ? (void *)h_dd_mode2 : (void *)h_dd_mode1;
    VirtualProtect(vt, 22 * sizeof(void *), old, &old);
    g_nddvt++;
    LOG("DirectDraw: interface %s patched to keep the desktop mode", v2 ? "IDirectDraw2+" : "IDirectDraw");
}

static HRESULT WINAPI h_DirectDrawCreate(GUID *g, void **out, void *outer)
{
    HRESULT hr = o_DirectDrawCreate(g, out, outer);
    if (SUCCEEDED(hr) && out && *out && g_keep_desktop) dd_patch(*out, 0);
    return hr;
}

#include <stdbool.h>
#include "pad_input.inc"
#include "pad_menu.inc"         /* gamepad navigation of the menus (0.7.4) */
#include "pad_extras.inc"       /* pad skips intros and auto replays, R3 CRT filter (0.7.12) */
#include <math.h>
#include "player_lod.inc"        /* player distance detail (0.7.8) */
#include "widescreen.inc"        /* Hor+ widescreen matches (0.7.5) */

static void setup_affinity(void)
{
    DWORD_PTR pm = 0, sm = 0;
    int cpu;
    GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm);
    g_pin = ini_int("PinGameThreads", 1);
    cpu = ini_int("GameCPU", -1);
    if (cpu >= 0 && cpu < (int)(8 * sizeof(DWORD_PTR)) && (sm & ((DWORD_PTR)1 << cpu)))
        g_game_mask = (DWORD_PTR)1 << cpu;
    else
        g_game_mask = pm & (~pm + 1);              /* lowest allowed CPU */
    if (sm && pm != sm) {
        /* an old shim pinned the whole process: give it all CPUs back */
        SetProcessAffinityMask(GetCurrentProcess(), sm);
        LOG("process affinity was 0x%lx (system 0x%lx): restored to all CPUs", (unsigned long)pm, (unsigned long)sm);
    }
    if (g_pin) {
        SetThreadAffinityMask(GetCurrentThread(), g_game_mask);   /* main thread (we run on it) */
        LOG("game threads pinned to CPU mask 0x%lx; driver, audio and other threads use all CPUs",
            (unsigned long)g_game_mask);
    } else {
        LOG("game thread pinning off (PinGameThreads=0)");
    }
}

#if NBA98_DEBUG
#include "diag.inc"          /* Debug build: crash reports, thread dumps, watches, system info */
#else
static void start_thread_dump(void) {}
#endif

/* ------------------------------------------------------------------------- */
/* stale nbatemp\ cleanup                                                    */
/* ------------------------------------------------------------------------- */
/* The game keeps its scratch files (player cards, list files) in nbatemp\ in
   the game folder and deletes the folder on a normal quit. Alt+F4 or a crash
   leaves it behind. Before any game code runs we delete the files directly in
   it and then the folder; subfolders (the game makes none) are left alone.
   A named mutex per game folder makes sure a second copy of the game started
   from the same folder never empties the folder of one already running.
   [nba98] CleanTempFolder=1 (default). */
static HANDLE g_folder_mutex;

static void clean_stale_nbatemp(void)
{
    char dir[MAX_PATH], pat[MAX_PATH], f[MAX_PATH], name[96];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    DWORD hash = 2166136261u;
    const char *p;
    int files = 0, failed = 0, subdirs = 0;
    if (!ini_int("CleanTempFolder", 1)) { LOG("nbatemp cleanup: off (CleanTempFolder=0)"); return; }
    for (p = g_exe_dir; *p; p++) hash = (hash ^ (unsigned char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p)) * 16777619u;
    _snprintf(name, sizeof name, "NBA98_GAME_FOLDER_%08lx", (unsigned long)hash);
    g_folder_mutex = CreateMutexA(NULL, FALSE, name);
    if (g_folder_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        LOG("nbatemp cleanup: skipped (another copy of the game is running from this folder)");
        return;
    }
    _snprintf(dir, sizeof dir, "%snbatemp", g_exe_dir);
    dir[sizeof dir - 1] = 0;
    {
        DWORD a = GetFileAttributesA(dir);
        if (a == INVALID_FILE_ATTRIBUTES) { LOG("nbatemp cleanup: no stale nbatemp folder"); return; }
        if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) {
            LOG("nbatemp cleanup: %s is not a plain folder, left alone", dir);
            return;
        }
    }
    _snprintf(pat, sizeof pat, "%s\\*", dir);
    pat[sizeof pat - 1] = 0;
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (strcmp(fd.cFileName, ".") && strcmp(fd.cFileName, "..")) subdirs++;
                continue;
            }
            _snprintf(f, sizeof f, "%s\\%s", dir, fd.cFileName);
            f[sizeof f - 1] = 0;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesA(f, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (DeleteFileA(f)) files++; else failed++;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (!failed && !subdirs && RemoveDirectoryA(dir))
        LOG("nbatemp cleanup: removed a stale nbatemp folder (%d file(s)), left by Alt+F4 or a crash", files);
    else
        LOG("nbatemp cleanup: deleted %d file(s) from a stale nbatemp folder; %d could not be deleted, %d subfolder(s) left alone; folder kept",
            files, failed, subdirs);
}

/* ------------------------------------------------------------------------- */
/* entry                                                                     */
/* ------------------------------------------------------------------------- */
static void init(HINSTANCE self)
{
    char path[MAX_PATH], *s;
    HMODULE exe = GetModuleHandleA(NULL);
    SYSTEMTIME st;

    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_qpc0);
    InitializeCriticalSection(&g_cs);
    GetModuleFileNameA(exe, path, sizeof path);
    strcpy(g_exe_dir, path);
    s = strrchr(g_exe_dir, '\\');
    if (s) s[1] = 0;
    _snprintf(g_ini, sizeof g_ini, "%snba98.ini", g_exe_dir);
    if (ini_int("Log", NBA98_LOG_DEFAULT)) {
        char logp[MAX_PATH];
        nba98_log_path(g_exe_dir, "nba98core.log", logp, sizeof logp);
        g_log = CreateFileA(logp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    GetLocalTime(&st);
    LOG("nba98core " NBA98_VERSION_LINE "  %04d-%02d-%02d %02d:%02d:%02d  exe %s  command line: %s",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, path, GetCommandLineA());
    (void)self;

    g_is_v11 = identify_exe();
    g_stats.game_v11 = g_is_v11;
    LOG("exe: %s", g_is_v11 ? "NBAWIN.EXE v1.1 (verified)" : "not the verified v1.1 build");

    o_CreateFileA = (CreateFileA_fn)iat_hook(exe, "KERNEL32.dll", "CreateFileA", (void *)h_CreateFileA);
    o_GlobalMemoryStatus = (GlobalMemoryStatus_fn)iat_hook(exe, "KERNEL32.dll", "GlobalMemoryStatus",
                                                           (void *)h_GlobalMemoryStatus);
    o_CreateThread = (CreateThread_fn)iat_hook(exe, "KERNEL32.dll", "CreateThread", (void *)h_CreateThread);
    o_timeSetEvent = (timeSetEvent_fn)iat_hook(exe, "WINMM.dll", "timeSetEvent", (void *)h_timeSetEvent);
    g_keep_desktop = ini_int("KeepDesktopMode", 1) && strstr(GetCommandLineA(), "3dfx") != NULL;
    if (g_keep_desktop) {
        o_DirectDrawCreate = (DDCreate_fn)iat_hook(exe, "DDRAW.dll", "DirectDrawCreate", (void *)h_DirectDrawCreate);
        LOG("DirectDraw: keep desktop mode %s", o_DirectDrawCreate ? "on" : "FAILED (no DirectDrawCreate import)");
    }
    pad_setup(exe);
    fscache_setup(exe);
    tempbuf_setup(exe);
    spchmem_setup(exe);
    g_safe_terminate = ini_int("SafeTerminate", 1);
    o_TerminateThread = (TerminateThread_fn)iat_hook(exe, "KERNEL32.dll", "TerminateThread", (void *)h_TerminateThread);
    if (!o_TerminateThread) o_TerminateThread = TerminateThread;
    if (g_safe_terminate) {
        o_EnterCS = (CS_fn)iat_hook(exe, "KERNEL32.dll", "EnterCriticalSection", (void *)h_EnterCS);
        o_LeaveCS = (CS_fn)iat_hook(exe, "KERNEL32.dll", "LeaveCriticalSection", (void *)h_LeaveCS);
        o_WaitSingle = (WaitSingle_fn)iat_hook(exe, "KERNEL32.dll", "WaitForSingleObject", (void *)h_WaitSingle);
        o_WaitMultiEx = (WaitMultiEx_fn)iat_hook(exe, "KERNEL32.dll", "WaitForMultipleObjectsEx", (void *)h_WaitMultiEx);
        o_SleepEx = (SleepEx_fn)iat_hook(exe, "KERNEL32.dll", "SleepEx", (void *)h_SleepEx);
        LOG("safe thread termination: hooks %s", (o_EnterCS && o_LeaveCS && o_WaitSingle && o_WaitMultiEx && o_SleepEx) ? "ok" : "INCOMPLETE");
        if (!o_EnterCS || !o_LeaveCS) g_safe_terminate = 0;
        if (!o_WaitSingle) o_WaitSingle = WaitForSingleObject;
        if (!o_WaitMultiEx) o_WaitMultiEx = WaitForMultipleObjectsEx;
        if (!o_SleepEx) o_SleepEx = SleepEx;
    }
    LOG("hooks: CreateFileA %s, GlobalMemoryStatus %s, CreateThread %s, timeSetEvent %s",
        o_CreateFileA ? "ok" : "MISSING", o_GlobalMemoryStatus ? "ok" : "MISSING",
        o_CreateThread ? "ok" : "MISSING", o_timeSetEvent ? "ok" : "MISSING");
    if (!o_CreateFileA) o_CreateFileA = CreateFileA;
    if (!o_GlobalMemoryStatus) o_GlobalMemoryStatus = GlobalMemoryStatus;
    if (!o_CreateThread) o_CreateThread = CreateThread;

    if (g_is_v11) patch_cd_check();
    roster_setup();
    art_setup();
    allstar_setup();
    optmenu_setup();
    skip_screens_setup();
    pad_menu_setup();
    pad_extras_setup();
    widescreen_setup();
    player_lod_setup();
    build_install_dat();
    setup_affinity();
    note_thread(GetCurrentThreadId());   /* main thread (we run in an APC on it) */
    SetCurrentDirectoryA(g_exe_dir);   /* the game opens install.dat and others relative to the cwd */
    clean_stale_nbatemp();             /* before any game code runs */
    timing_setup();
    input_frames_setup();
    start_thread_dump();
    {
        char ev[64];
        HANDLE h;
        _snprintf(ev, sizeof ev, "NBA98CORE_READY_%lu", GetCurrentProcessId());
        h = OpenEventA(EVENT_MODIFY_STATE, FALSE, ev);
        if (h) { SetEvent(h); CloseHandle(h); }
    }
    LOG("setup done");
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE pinned;
        DisableThreadLibraryCalls(inst);
        /* never unload: our hooks live here */
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCSTR)(void *)DllMain, &pinned);
        init(inst);
    }
    return TRUE;
}

/* exported so the launcher (or a test) can confirm the DLL is the right one */
__declspec(dllexport) const char *nba98core_version(void) { return "nba98core " NBA98_VERSION_LINE; }
NBA98_C_END
