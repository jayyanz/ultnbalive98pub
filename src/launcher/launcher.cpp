/*
 * NBALive98.exe - launcher for the NBA Live 98 compatibility build.
 *
 * Starts the unmodified game exe (NBAWIN.EXE, or nba98.exe) from its own
 * folder with the "3dfx" argument, and loads nba98core.dll into it before any
 * game code runs: the process is created suspended and LoadLibraryA is queued
 * as an APC on its main thread, which Windows runs right after the exe's own
 * imports are loaded and before the exe's entry point. Works the same under
 * Wine/Proton, unlike a fake system DLL next to the exe.
 *
 * Any extra command line arguments are passed on to the game.
 * Releases ship nba98.ini.default; the launcher copies it to nba98.ini only
 * when there is no nba98.ini yet, so updates keep the user's settings.
 * Settings (optional) in nba98.ini, section [launcher]:
 *   Exe=NBAWIN.EXE        game exe to start
 *   Args=3dfx             arguments given to the game before any of ours
 *   Log=0/1               logs\launcher.log (default on in the Debug build)
 *   WatchGame=1           Debug build: report a crash (exception exit code)
 *                         with a message when the game closes
 *
 * Restart (0.7.1): both builds wait for the game to close. If the settings
 * screens asked for a restart (nba98data\restart.req, written by the core when
 * the player answers Yes to "Restart now?") and the game exited cleanly (exit
 * code 0), the launcher deletes the request and starts the game again with the
 * same arguments. Never after a crash. The environment variable
 * NBA98_RESTART_OK=1 tells the core that a launcher is waiting, so the
 * question is only asked when the restart can happen.
 *
 * Both builds check that nba98core.dll, glide2x.dll and eacsnd.dll carry the
 * same version and build (Main or Debug) as the launcher, and warn if not.
 */
#define WIN32_LEAN_AND_MEAN
#include "nba98_cxx.h"   /* C++ build settings, first (0.7.15) */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "nba98_build.h"

NBA98_C_BEGIN
static char g_dir[MAX_PATH];
static HANDLE g_log = INVALID_HANDLE_VALUE;

static void lg(const char *fmt, ...)
{
    char buf[1024];
    int n;
    va_list ap;
    DWORD w;
    SYSTEMTIME st;
    if (g_log == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&st);
    n = _snprintf(buf, 32, "[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    va_start(ap, fmt);
    n += _vsnprintf(buf + n, sizeof buf - n - 3, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    WriteFile(g_log, buf, (DWORD)n, &w, NULL);
}

/* FileVersion string of one of our files ("" if it has none) */
static void file_version(const char *name, char *out, int cap)
{
    char path[MAX_PATH];
    DWORD h, n;
    void *buf, *val;
    UINT len;
    out[0] = 0;
    _snprintf(path, sizeof path, "%s%s", g_dir, name);
    n = GetFileVersionInfoSizeA(path, &h);
    if (!n || !(buf = HeapAlloc(GetProcessHeap(), 0, n))) return;
    if (GetFileVersionInfoA(path, 0, n, buf) && VerQueryValueA(buf, "\\StringFileInfo\\0c0904b0\\FileVersion", &val, &len) && len)
        _snprintf(out, cap, "%s", (char *)val);
    out[cap - 1] = 0;
    HeapFree(GetProcessHeap(), 0, buf);
}

/* warn when our files come from different versions or builds; returns 0 to stop */
static int check_versions(void)
{
    static const char *files[] = { "nba98core.dll", "glide2x.dll", "eacsnd.dll" };
    char msg[1500], v[128];
    int i, bad = 0, len;
    len = _snprintf(msg, sizeof msg, "These files do not match this launcher (%s):\n\n", NBA98_VERSION_LINE);
    for (i = 0; i < 3; i++) {
        char path[MAX_PATH];
        _snprintf(path, sizeof path, "%s%s", g_dir, files[i]);
        file_version(files[i], v, sizeof v);
        lg("%-14s %s", files[i], GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? "missing" : v[0] ? v : "no version");
        if (strcmp(v, NBA98_VERSION_LINE) != 0) {
            bad = 1;
            len += _snprintf(msg + len, sizeof msg - len, "%s: %s\n", files[i],
                             GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? "missing" : v[0] ? v : "older file, no version");
        }
    }
    if (!bad) return 1;
    _snprintf(msg + len, sizeof msg - len, "\nCopy all files from one release (Main or Debug) into the game folder.\n"
              "Start the game anyway?");
    msg[sizeof msg - 1] = 0;
    return MessageBoxA(NULL, msg, "NBA Live 98 launcher", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDYES;
}

#if NBA98_DEBUG
/* newest crash\*.txt written since 'since' (file time), or "" */
static void newest_crash_report(const FILETIME *since, char *out, int cap)
{
    WIN32_FIND_DATAA fd;
    FILETIME best = *since;
    char pat[MAX_PATH];
    HANDLE h;
    out[0] = 0;
    _snprintf(pat, sizeof pat, "%scrash\\nba98_*.txt", g_dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (CompareFileTime(&fd.ftLastWriteTime, &best) >= 0) {
            best = fd.ftLastWriteTime;
            _snprintf(out, cap, "crash\\%s", fd.cFileName);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#endif

static void fail(const char *fmt, const char *a, DWORD err)
{
    char msg[1024];
    _snprintf(msg, sizeof msg, fmt, a, err);
    msg[sizeof msg - 1] = 0;
    MessageBoxA(NULL, msg, "NBA Live 98 launcher", MB_ICONERROR);
}

static int file_exists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Settings survive updates: releases ship nba98.ini.default, never nba98.ini.
   On the first start (no nba98.ini yet) the user's nba98.ini is made from the
   default; after that it is never replaced, so copying a new release over an
   install keeps the user's settings (including the in-game Enhanced Options).
   Returns 0 kept the existing file, 1 created, 2 neither file exists,
   -1 the copy failed (the game then runs on built-in defaults). */
static int make_ini(const char *ini)
{
    char def[MAX_PATH];
    if (file_exists(ini)) return 0;
    _snprintf(def, sizeof def, "%s.default", ini);
    def[sizeof def - 1] = 0;
    if (!file_exists(def)) return 2;
    return CopyFileA(def, ini, TRUE) ? 1 : -1;   /* TRUE: never overwrite */
}

/* the part of our command line after the program name */
static const char *passthrough_args(void)
{
    const char *c = GetCommandLineA();
    if (*c == '"') { c++; while (*c && *c != '"') c++; if (*c) c++; }
    else while (*c && *c != ' ' && *c != '\t') c++;
    while (*c == ' ' || *c == '\t') c++;
    return c;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    char ini[MAX_PATH], exe[MAX_PATH], exe_name[MAX_PATH], args[256], cmdline[2048], dll[MAX_PATH], evname[64];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    LPVOID remote;
    SIZE_T wrote;
    HANDLE ready;
    FARPROC loadlib;
    char *s, marker[MAX_PATH];
    DWORD wr;
    int ini_made;
    (void)inst; (void)prev; (void)cmd; (void)show;

    GetModuleFileNameA(NULL, g_dir, sizeof g_dir);
    s = strrchr(g_dir, '\\');
    if (s) s[1] = 0;
    _snprintf(ini, sizeof ini, "%snba98.ini", g_dir);
    ini_made = make_ini(ini);
    if (GetPrivateProfileIntA("launcher", "Log", NBA98_LOG_DEFAULT, ini)) {
        char lp[MAX_PATH];
        nba98_log_path(g_dir, "launcher.log", lp, sizeof lp);
        g_log = CreateFileA(lp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    lg("NBA Live 98 launcher %s, folder %s", NBA98_VERSION_LINE, g_dir);
    lg("settings: %s", ini_made == 1 ? "nba98.ini created from nba98.ini.default (first start)" :
                       ini_made < 0 ? "no nba98.ini and it could not be created from nba98.ini.default (built-in defaults)" :
                       ini_made == 2 ? "no nba98.ini or nba98.ini.default (built-in defaults)" : "using the existing nba98.ini");
    if (!check_versions()) { lg("stopped: file versions do not match"); return 1; }

    GetPrivateProfileStringA("launcher", "Exe", "", exe_name, sizeof exe_name, ini);
    if (exe_name[0]) {
        _snprintf(exe, sizeof exe, "%s%s", g_dir, exe_name);
    } else {
        _snprintf(exe, sizeof exe, "%sNBAWIN.EXE", g_dir);
        if (!file_exists(exe)) _snprintf(exe, sizeof exe, "%snba98.exe", g_dir);
    }
    if (!file_exists(exe)) {
        fail("Could not find the game exe (NBAWIN.EXE or nba98.exe) in\n%s", g_dir, 0);
        return 1;
    }
    _snprintf(dll, sizeof dll, "%snba98core.dll", g_dir);
    if (!file_exists(dll)) {
        fail("nba98core.dll is missing from\n%s", g_dir, 0);
        return 1;
    }
    GetPrivateProfileStringA("launcher", "Args", "3dfx", args, sizeof args, ini);
    _snprintf(cmdline, sizeof cmdline, "\"%s\" %s %s", exe, args, passthrough_args());
    cmdline[sizeof cmdline - 1] = 0;

    /* the core offers "restart now" only when we are here to start the game again */
    SetEnvironmentVariableA("NBA98_RESTART_OK", "1");
    _snprintf(marker, sizeof marker, "%snba98data\\restart.req", g_dir);
    marker[sizeof marker - 1] = 0;
    DeleteFileA(marker);   /* a stale request never restarts anything */

    for (;;) {
        DWORD code = 0;
        {   /* each game start logs to its own folder (logs\<date>_<time>_<version>_<build>_<PC>) */
            static int starts;
            char ses[128];
            if (starts++) nba98_log_session_new(ses, sizeof ses); else nba98_log_session(ses, sizeof ses);
            lg("logs for this start: logs\\%s", ses);
        }
        lg("starting: %s", cmdline);
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        if (!CreateProcessA(exe, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, g_dir, &si, &pi)) {
            fail("Could not start %s (error %lu)", exe, GetLastError());
            return 1;
        }

        /* the core DLL signals this event once it has finished its setup */
        _snprintf(evname, sizeof evname, "NBA98CORE_READY_%lu", pi.dwProcessId);
        ready = CreateEventA(NULL, TRUE, FALSE, evname);

        remote = VirtualAllocEx(pi.hProcess, NULL, strlen(dll) + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        loadlib = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
        if (!remote || !WriteProcessMemory(pi.hProcess, remote, dll, strlen(dll) + 1, &wrote) ||
            !QueueUserAPC((PAPCFUNC)loadlib, pi.hThread, (ULONG_PTR)remote)) {
            DWORD e = GetLastError();
            TerminateProcess(pi.hProcess, 1);
            fail("Could not load nba98core.dll into the game%s (error %lu)", "", e);
            return 1;
        }
        ResumeThread(pi.hThread);

        wr = ready ? WaitForSingleObject(ready, 15000) : WAIT_FAILED;
        if (wr != WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &code);
            if (code == STILL_ACTIVE)
                MessageBoxA(NULL, "The game started but nba98core.dll did not report in.\n"
                                  "The CD check and install folder fixes may not be active.\n"
                                  "Set Log=1 under [nba98] in nba98.ini and see logs\\nba98core.log.",
                            "NBA Live 98 launcher", MB_ICONWARNING);
        }
        lg("game started (process %lu), core %s", pi.dwProcessId, wr == WAIT_OBJECT_0 ? "ready" : "did not report in");

        /* Both builds wait for the game (no window, no CPU use), so a "restart now" from the
           settings screens can start it again. */
        {
#if NBA98_DEBUG
            FILETIME start;
            SYSTEMTIME now;
            GetSystemTime(&now);
            SystemTimeToFileTime(&now, &start);
            {   /* reports are stamped to the second: allow for that */
                ULARGE_INTEGER u; u.LowPart = start.dwLowDateTime; u.HighPart = start.dwHighDateTime;
                u.QuadPart -= 10000000ULL * 120; start.dwLowDateTime = u.LowPart; start.dwHighDateTime = u.HighPart;
            }
#endif
            WaitForSingleObject(pi.hProcess, INFINITE);
            code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            lg("game closed, exit code 0x%08lx", code);
#if NBA98_DEBUG
            if (code >= 0xC0000000u && GetPrivateProfileIntA("launcher", "WatchGame", 1, ini)) {
                char rep[MAX_PATH], msg[1200];
                newest_crash_report(&start, rep, sizeof rep);
                lg("crash report: %s", rep[0] ? rep : "none written");
                _snprintf(msg, sizeof msg, "NBA Live 98 crashed (exception 0x%08lX).\n\n%s%s%s"
                          "The logs folder has the details of this session.", code,
                          rep[0] ? "Crash report: " : "No crash report was written.\n", rep[0] ? rep : "",
                          rep[0] ? " (and the .dmp file with the same name)\n" : "");
                msg[sizeof msg - 1] = 0;
                MessageBoxA(NULL, msg, "NBA Live 98 (Debug build)", MB_ICONERROR);
            }
#endif
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (ready) CloseHandle(ready);
        ready = NULL;

        /* restart requested from the settings screens: once per request, only after a clean exit */
        if (!file_exists(marker)) break;
        DeleteFileA(marker);
        if (code != 0) { lg("restart requested but the game did not exit cleanly (0x%08lx): not restarted", code); break; }
        lg("restart requested from the settings screens: starting the game again");
    }
    if (g_log != INVALID_HANDLE_VALUE) CloseHandle(g_log);
    return 0;
}
NBA98_C_END
