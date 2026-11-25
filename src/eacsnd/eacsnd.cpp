/*
 * eacsnd.dll for NBA Live 98 - the game's sound output driver, on SDL3.
 *
 * The original eacsnd.dll (9 KB, Watcom) owns a DirectSound buffer and nothing
 * else: the mixer is in the exe. It is the game's only user of DSOUND.dll, so
 * replacing it removes DirectSound (and ALchemy's dsound.dll) from the chain.
 *
 * Interface (Watcom register calling convention: arguments in EAX, EDX, EBX,
 * ECX, then the stack; the callee keeps every register except EAX; result in
 * EAX). Found by disassembling the original DLL and the exe (loader 0x536008):
 *
 *  iSNDdllversion_()                   -> 0x60002 (the exe refuses anything else)
 *  iSNDdirectsetfunctions_(mix, log, f3, tick, [stack] f5)   ret 4, -> 0
 *      mix  (0x57245C)  Watcom: EAX = destination, EDX = frames. Mixes that many
 *                       frames in the output format into the destination.
 *      log              cdecl printf-style, NULL in practice
 *      tick ([0x5DBFB0]) called every 1/100 s of mixed audio, between mix calls;
 *                       drives the game's music and speech sequencing
 *      f3, f5           unused by the original
 *  iSNDdirectcaps_(hwnd)               -> bit mask of supported formats, < 0 on error
 *      bits 0..3 mono8, stereo8, mono16, stereo16; bits 9..13 11025, 16000,
 *      22050, 32000, 44100 Hz; bit 17 = emulated (software) driver
 *  iSNDdirectstart_(flags, hwnd)       -> 0 or < 0. flags use the same bits; the
 *      original picks 22050, 16000, 32000, 44100, 11025 and stereo16, stereo8,
 *      mono16, mono8 in that order of preference.
 *  iSNDdirectstop_()                   -> 0
 *  iSNDdirectserve_()                  -> called by the game's multimedia timer
 *      (every 1 to 18 ms, inside the game's sound lock) to top up the output.
 *
 * Like the original, all mixing happens inside serve(), on the game's thread and
 * under its lock, so the game's mixer is never called from another thread. We
 * keep the SDL audio stream filled to LatencyMs and SDL plays it.
 *
 * Settings in nba98.ini [sound]: LatencyMs (default 40), DeviceFrames (SDL
 * device buffer, default 0 = SDL's choice), Log (default 1, eacsnd.log).
 */
#define WIN32_LEAN_AND_MEAN
#include "nba98_cxx.h"   /* C++ build settings, first (0.7.15) */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include "nba98_build.h"

NBA98_C_BEGIN
/* ---- the few SDL3 declarations we need (SDL3.dll is loaded at run time) ---- */
typedef uint32_t SDL_AudioDeviceID;
typedef int SDL_AudioFormat;
typedef struct { SDL_AudioFormat format; int channels; int freq; } SDL_AudioSpec;
typedef struct SDL_AudioStream SDL_AudioStream;
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)
#define SDL_AUDIO_U8 0x0008
#define SDL_AUDIO_S16LE 0x8010

typedef bool (__cdecl *pSDL_Init)(uint32_t);
typedef bool (__cdecl *pSDL_SetHint)(const char *, const char *);
typedef const char *(__cdecl *pSDL_GetError)(void);
typedef const char *(__cdecl *pSDL_GetCurrentAudioDriver)(void);
typedef SDL_AudioStream *(__cdecl *pSDL_OpenAudioDeviceStream)(SDL_AudioDeviceID, const SDL_AudioSpec *, void *, void *);
typedef bool (__cdecl *pSDL_ResumeAudioStreamDevice)(SDL_AudioStream *);
typedef bool (__cdecl *pSDL_PutAudioStreamData)(SDL_AudioStream *, const void *, int);
typedef int (__cdecl *pSDL_GetAudioStreamQueued)(SDL_AudioStream *);
typedef void (__cdecl *pSDL_DestroyAudioStream)(SDL_AudioStream *);
typedef SDL_AudioDeviceID (__cdecl *pSDL_GetAudioStreamDevice)(SDL_AudioStream *);
typedef bool (__cdecl *pSDL_GetAudioDeviceFormat)(SDL_AudioDeviceID, SDL_AudioSpec *, int *);

static struct {
    HMODULE dll;
    pSDL_Init Init;
    pSDL_SetHint SetHint;
    pSDL_GetError GetError;
    pSDL_GetCurrentAudioDriver GetCurrentAudioDriver;
    pSDL_OpenAudioDeviceStream OpenAudioDeviceStream;
    pSDL_ResumeAudioStreamDevice ResumeAudioStreamDevice;
    pSDL_PutAudioStreamData PutAudioStreamData;
    pSDL_GetAudioStreamQueued GetAudioStreamQueued;
    pSDL_DestroyAudioStream DestroyAudioStream;
    pSDL_GetAudioStreamDevice GetAudioStreamDevice;
    pSDL_GetAudioDeviceFormat GetAudioDeviceFormat;
    int inited;
} sdl;

/* ---- state ---------------------------------------------------------------- */
static void *g_mix, *g_log, *g_tick;
static SDL_AudioStream *g_stream;
static int g_running;
static int g_rate, g_channels, g_bits, g_shift;     /* shift = log2(bytes per frame) */
static int g_target_frames;                        /* how far ahead we keep the queue */
static int g_tick_counter, g_tick_acc, g_until_tick;
static uint8_t *g_buf;
static int g_buf_frames;
static char g_dir[MAX_PATH];
static int g_cfg_latency_ms, g_cfg_device_frames, g_cfg_log;
static HANDLE g_logf = INVALID_HANDLE_VALUE;
static LARGE_INTEGER g_qpf, g_qpc0;

/* stats */
static double g_last_serve_ms, g_max_gap_ms, g_stat_t0;
static long g_serves, g_underruns, g_frames_mixed;

static double now_ms(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)(c.QuadPart - g_qpc0.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
}

static void slog(const char *fmt, ...)
{
    char buf[1024];
    int n;
    DWORD w;
    va_list ap;
    if (g_logf == INVALID_HANDLE_VALUE) return;
    n = _snprintf(buf, sizeof buf, "[%10.1f ms] ", now_ms());
    va_start(ap, fmt);
    n += _vsnprintf(buf + n, sizeof buf - n - 3, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    WriteFile(g_logf, buf, (DWORD)n, &w, NULL);
}

static int load_sdl(void)
{
    char path[MAX_PATH];
    if (sdl.dll) return 1;
    _snprintf(path, sizeof path, "%sSDL3.dll", g_dir);
    sdl.dll = LoadLibraryA(path);
    if (!sdl.dll) sdl.dll = LoadLibraryA("SDL3.dll");
    if (!sdl.dll) { slog("ERROR: SDL3.dll not found (put it next to NBAWIN.EXE)"); return 0; }
#define GET(n) sdl.n = (p##SDL_##n)GetProcAddress(sdl.dll, "SDL_" #n); if (!sdl.n) { slog("ERROR: SDL3.dll has no SDL_" #n); return 0; }
    GET(Init) GET(SetHint) GET(GetError) GET(GetCurrentAudioDriver) GET(OpenAudioDeviceStream)
    GET(ResumeAudioStreamDevice) GET(PutAudioStreamData) GET(GetAudioStreamQueued)
    GET(DestroyAudioStream) GET(GetAudioStreamDevice) GET(GetAudioDeviceFormat)
#undef GET
    return 1;
}

/* ---- calls into the game (Watcom register convention) ---------------------- */
static void call_watcom2(void *fn, uint32_t a, uint32_t b)
{
    __asm__ volatile(
        "pushl %%ebp\n\t"
        "call *%%esi\n\t"
        "popl %%ebp\n\t"
        : "+a"(a), "+d"(b)
        : "S"(fn)
        : "ebx", "ecx", "edi", "memory", "cc");
}

static void game_mix(uint8_t *dst, int frames)
{
    if (g_mix) call_watcom2(g_mix, (uint32_t)(uintptr_t)dst, (uint32_t)frames);
    else memset(dst, g_bits == 8 ? 0x80 : 0, (size_t)frames << g_shift);
}

/* Same pacing as the original: the tick runs every 1/100 s of audio, chunk
 * sizes rounded to 16 frames, the error carried in an accumulator. */
static void fill(uint8_t *dst, int frames)
{
    while (frames > 0) {
        int n;
        if (g_until_tick <= 0) {
            int t;
            g_tick_counter++;
            if (g_tick) call_watcom2(g_tick, 0, 0);
            t = (int)(((unsigned)g_tick_counter * (unsigned)g_rate) / 100u) - g_tick_acc;
            t &= 0x0ffffff0;
            g_tick_acc += t;
            g_until_tick = t;
            if (g_tick_counter > 30000) { g_tick_counter = 0; g_tick_acc = 0; }
            continue;
        }
        n = frames < g_until_tick ? frames : g_until_tick;
        g_until_tick -= n;
        game_mix(dst, n);
        dst += n << g_shift;
        frames -= n;
    }
}

/* ---- the six exports (C side) ---------------------------------------------- */
int snd_dllversion(void) __asm__("snd_dllversion");
int snd_setfunctions(uint32_t eax, uint32_t edx, uint32_t ebx, uint32_t ecx, uint32_t s1) __asm__("snd_setfunctions");
int snd_caps(uint32_t eax) __asm__("snd_caps");
int snd_start(uint32_t eax, uint32_t edx) __asm__("snd_start");
int snd_stop(void) __asm__("snd_stop");
int snd_serve(void) __asm__("snd_serve");

int snd_dllversion(void)
{
    return 0x60002;
}

int snd_setfunctions(uint32_t mix, uint32_t log, uint32_t f3, uint32_t tick, uint32_t f5)
{
    g_mix = (void *)(uintptr_t)mix;
    g_log = (void *)(uintptr_t)log;
    g_tick = (void *)(uintptr_t)tick;
    slog("setfunctions: mix 0x%08lx, log 0x%08lx, tick 0x%08lx (unused 0x%08lx, 0x%08lx)",
         (unsigned long)mix, (unsigned long)log, (unsigned long)tick, (unsigned long)f3, (unsigned long)f5);
    return 0;
}

static int init_sdl(void)
{
    if (sdl.inited) return 1;
    if (!load_sdl()) return 0;
    if (g_cfg_device_frames > 0) {
        char v[16];
        _snprintf(v, sizeof v, "%d", g_cfg_device_frames);
        sdl.SetHint("SDL_AUDIO_DEVICE_SAMPLE_FRAMES", v);
    }
    sdl.SetHint("SDL_AUDIO_DEVICE_STREAM_NAME", "NBA Live 98");
    sdl.SetHint("SDL_APP_NAME", "NBA Live 98");
    if (!sdl.Init(SDL_INIT_AUDIO)) {
        slog("ERROR: SDL audio init failed: %s", sdl.GetError());
        return 0;
    }
    slog("SDL audio driver: %s", sdl.GetCurrentAudioDriver() ? sdl.GetCurrentAudioDriver() : "?");
    sdl.inited = 1;
    return 1;
}

int snd_caps(uint32_t hwnd)
{
    int caps;
    (void)hwnd;
    if (!init_sdl()) return -1;
    caps = 0x0F            /* mono8, stereo8, mono16, stereo16 */
         | 0x3E << 8;      /* 11025, 16000, 22050, 32000, 44100 */
    slog("caps -> 0x%x", caps);
    return caps;
}

int snd_stop(void)
{
    if (g_stream) {
        sdl.DestroyAudioStream(g_stream);
        g_stream = NULL;
        slog("stop: %ld serves, %ld underruns, longest gap between serves %.1f ms",
             g_serves, g_underruns, g_max_gap_ms);
    }
    g_running = 0;
    return 0;
}

int snd_start(uint32_t flags, uint32_t hwnd)
{
    SDL_AudioSpec spec, dspec;
    int dframes = 0;
    unsigned hi = (flags >> 8) & 0xFF, lo = flags & 0xFF;
    (void)hwnd;
    if (g_running) snd_stop();
    if (!init_sdl()) return -1;

    if (hi & 0x08) g_rate = 22050;
    else if (hi & 0x04) g_rate = 16000;
    else if (hi & 0x10) g_rate = 32000;
    else if (hi & 0x20) g_rate = 44100;
    else if (hi & 0x02) g_rate = 11025;
    else { slog("start: no sample rate in flags 0x%lx", (unsigned long)flags); return -1; }
    if (lo & 0x08) { g_channels = 2; g_bits = 16; }
    else if (lo & 0x02) { g_channels = 2; g_bits = 8; }
    else if (lo & 0x04) { g_channels = 1; g_bits = 16; }
    else if (lo & 0x01) { g_channels = 1; g_bits = 8; }
    else { slog("start: no sample format in flags 0x%lx", (unsigned long)flags); return -1; }
    g_shift = (g_channels == 2 ? 1 : 0) + (g_bits == 16 ? 1 : 0);

    spec.format = g_bits == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8;
    spec.channels = g_channels;
    spec.freq = g_rate;
    g_stream = sdl.OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!g_stream) {
        slog("ERROR: could not open the audio device: %s", sdl.GetError());
        return -1;
    }

    g_target_frames = (g_rate * g_cfg_latency_ms / 1000) & ~15;
    if (g_target_frames < 256) g_target_frames = 256;
    if (g_buf_frames < g_target_frames) {
        if (g_buf) HeapFree(GetProcessHeap(), 0, g_buf);
        g_buf_frames = g_target_frames;
        g_buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, (size_t)g_buf_frames * 4);
    }
    g_tick_counter = g_tick_acc = g_until_tick = 0;
    g_serves = g_underruns = g_frames_mixed = 0;
    g_max_gap_ms = 0;
    g_last_serve_ms = 0;
    g_stat_t0 = now_ms();

    /* start with half the queue as silence (like the original's cleared buffer);
       the game's mixer is only ever called from serve() */
    memset(g_buf, g_bits == 8 ? 0x80 : 0, (size_t)g_target_frames << g_shift);
    g_target_frames &= ~15;
    sdl.PutAudioStreamData(g_stream, g_buf, (g_target_frames / 2) << g_shift);
    sdl.ResumeAudioStreamDevice(g_stream);
    g_running = 1;

    memset(&dspec, 0, sizeof dspec);
    sdl.GetAudioDeviceFormat(sdl.GetAudioStreamDevice(g_stream), &dspec, &dframes);
    slog("start: flags 0x%lx -> %d Hz, %d-bit, %s; kept %d ms ahead; device %d Hz, %d ch, %d frames per buffer",
         (unsigned long)flags, g_rate, g_bits, g_channels == 2 ? "stereo" : "mono", g_cfg_latency_ms,
         dspec.freq, dspec.channels, dframes);
    return 0;
}

int snd_serve(void)
{
    int queued, need;
    double t;
    if (!g_running || !g_stream) return 0;
    t = now_ms();
    if (g_last_serve_ms > 0 && t - g_last_serve_ms > g_max_gap_ms) g_max_gap_ms = t - g_last_serve_ms;
    g_last_serve_ms = t;
    g_serves++;

    queued = sdl.GetAudioStreamQueued(g_stream);
    if (queued < 0) return 0;
    queued >>= g_shift;
    if (queued == 0) g_underruns++;
    need = (g_target_frames - queued) & ~15;
    if (need >= 16) {
        if (need > g_buf_frames) need = g_buf_frames;
        fill(g_buf, need);
        sdl.PutAudioStreamData(g_stream, g_buf, need << g_shift);
        g_frames_mixed += need;
    }
    if (t - g_stat_t0 >= 60000.0) {
        slog("last minute: %ld serves, %ld underruns, longest gap between serves %.1f ms",
             g_serves, g_underruns, g_max_gap_ms);
        g_serves = g_underruns = 0;
        g_max_gap_ms = 0;
        g_stat_t0 = t;
    }
    return 0;
}

/* ---- Watcom-convention entry points ---------------------------------------- */
/* Save every register but EAX, pass EAX, EDX, EBX, ECX (and stack args) to C. */
__asm__(
    ".text\n"
    ".globl _iSNDdllversion_\n"
    "_iSNDdllversion_:\n"
    "  pushl %ebp; pushl %edi; pushl %esi; pushl %ecx; pushl %ebx; pushl %edx\n"
    "  call snd_dllversion\n"
    "  popl %edx; popl %ebx; popl %ecx; popl %esi; popl %edi; popl %ebp\n"
    "  ret\n"

    ".globl _iSNDdirectsetfunctions_\n"
    "_iSNDdirectsetfunctions_:\n"
    "  pushl %ebp; pushl %edi; pushl %esi; pushl %ecx; pushl %ebx; pushl %edx\n"
    "  pushl 28(%esp)\n"                        /* 5th argument (on the stack) */
    "  pushl %ecx; pushl %ebx; pushl %edx; pushl %eax\n"
    "  call snd_setfunctions\n"
    "  addl $20, %esp\n"
    "  popl %edx; popl %ebx; popl %ecx; popl %esi; popl %edi; popl %ebp\n"
    "  ret $4\n"

    ".globl _iSNDdirectcaps_\n"
    "_iSNDdirectcaps_:\n"
    "  pushl %ebp; pushl %edi; pushl %esi; pushl %ecx; pushl %ebx; pushl %edx\n"
    "  pushl %eax\n"
    "  call snd_caps\n"
    "  addl $4, %esp\n"
    "  popl %edx; popl %ebx; popl %ecx; popl %esi; popl %edi; popl %ebp\n"
    "  ret\n"

    ".globl _iSNDdirectstart_\n"
    "_iSNDdirectstart_:\n"
    "  pushl %ebp; pushl %edi; pushl %esi; pushl %ecx; pushl %ebx; pushl %edx\n"
    "  pushl %edx; pushl %eax\n"
    "  call snd_start\n"
    "  addl $8, %esp\n"
    "  popl %edx; popl %ebx; popl %ecx; popl %esi; popl %edi; popl %ebp\n"
    "  ret\n"

    ".globl _iSNDdirectstop_\n"
    "_iSNDdirectstop_:\n"
    "  pushl %ebp; pushl %edi; pushl %esi; pushl %ecx; pushl %ebx; pushl %edx\n"
    "  call snd_stop\n"
    "  popl %edx; popl %ebx; popl %ecx; popl %esi; popl %edi; popl %ebp\n"
    "  ret\n"

    ".globl _iSNDdirectserve_\n"
    "_iSNDdirectserve_:\n"
    "  pushl %ebp; pushl %edi; pushl %esi; pushl %ecx; pushl %ebx; pushl %edx\n"
    "  call snd_serve\n"
    "  popl %edx; popl %ebx; popl %ecx; popl %esi; popl %edi; popl %ebp\n"
    "  ret\n");

/* ---- DLL entry -------------------------------------------------------------- */
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        char ini[MAX_PATH], *s;
        SYSTEMTIME st;
        DisableThreadLibraryCalls(inst);
        QueryPerformanceFrequency(&g_qpf);
        QueryPerformanceCounter(&g_qpc0);
        GetModuleFileNameA(inst, g_dir, sizeof g_dir);
        s = strrchr(g_dir, '\\');
        if (s) s[1] = 0;
        _snprintf(ini, sizeof ini, "%snba98.ini", g_dir);
        g_cfg_latency_ms = (int)GetPrivateProfileIntA("sound", "LatencyMs", 40, ini);
        if (g_cfg_latency_ms < 10) g_cfg_latency_ms = 10;
        if (g_cfg_latency_ms > 500) g_cfg_latency_ms = 500;
        g_cfg_device_frames = (int)GetPrivateProfileIntA("sound", "DeviceFrames", 0, ini);
        g_cfg_log = (int)GetPrivateProfileIntA("sound", "Log", NBA98_LOG_DEFAULT, ini);
        if (g_cfg_log) {
            char path[MAX_PATH];
            nba98_log_path(g_dir, "eacsnd.log", path, sizeof path);
            g_logf = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        }
        GetLocalTime(&st);
        slog("NBA Live 98 eacsnd.dll (SDL3) " NBA98_VERSION_LINE "  %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    } else if (reason == DLL_PROCESS_DETACH) {
        /* no SDL calls here: the loader lock is held */
        if (g_logf != INVALID_HANDLE_VALUE) CloseHandle(g_logf);
        g_logf = INVALID_HANDLE_VALUE;
        (void)reserved;
    }
    return TRUE;
}
NBA98_C_END
