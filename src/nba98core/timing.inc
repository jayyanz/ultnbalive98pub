/*
 * Timing statistics (included by nba98core.cpp). See common/nba98_stats.h.
 *
 * - Game clock: the game's 120 Hz timer callback (0x5253A4) runs through our
 *   timeSetEvent wrapper; every call is counted and the gap since the last
 *   one measured with QueryPerformanceCounter.
 * - Simulation steps: the match loop's step block starts at 0x415A0C with
 *   "mov eax, [0x5E7ED1]" (5 bytes). That instruction is replaced by a call to
 *   a thunk that counts the step and does the same load (v1.1 exe only).
 *   Nothing in the game's state is changed.
 * - Precise clock ([nba98] PreciseClock=1, default): the game's timer
 *   callback re-arms itself from GetTickCount, which on Windows only moves
 *   every 15.6 ms, so its 120 Hz ticks came in clumps (gaps up to 20 ms in
 *   Jason's first timing log). The exe's GetTickCount import is replaced by a
 *   millisecond clock from QueryPerformanceCounter (same starting value, so it
 *   wraps like the real one), and timer resolution is set to 1 ms.
 * - Step lock support: after each finished game timer callback, ticks_done
 *   goes up and tick_event is set, so glide2x can present each frame right
 *   after the game clock has made the next input frame (see gl_d3d.cpp).
 * - A sampler thread publishes per-second figures for the overlay and, with
 *   [nba98] TimingLog=1 (default in the Debug build), writes one line per
 *   second to logs\timing.log together with glide2x's frame figures.
 */
#include "nba98_stats.h"
#include <stddef.h>

nba98_stats g_stats __asm__("nba98_g_stats") = { NBA98_STATS_VERSION, sizeof(nba98_stats) };
static_assert(offsetof(nba98_stats, sim_steps) == 8, "the step thunk uses offset 8");

__declspec(dllexport) nba98_stats *nba98core_stats(void) { return &g_stats; }

#define STEP_PATCH_VA 0x00415A0C
void step_thunk(void) __asm__("nba98_step_thunk");
__asm__(".text\n"
        "nba98_step_thunk:\n"
        "  lock incl nba98_g_stats+8\n"
        "  movl 0x005E7ED1, %eax\n"
        "  ret\n");

static LPTIMECALLBACK g_clock_cb;     /* the timer callback we measure */
static LARGE_INTEGER g_tick_last;
static double g_gap_min = 1e9, g_gap_max, g_gap_sum;
static long g_gap_n;
static HANDLE g_tlog = INVALID_HANDLE_VALUE;

/* precise GetTickCount for the game. The clock can run slightly faster or
   slower than real time (display lock, set by glide2x): it is kept as an
   anchor (QPC, ms, rate) in one of two slots; a rate change fills the other
   slot from the current reading and flips the index, so readers on other
   threads never see a half-written anchor and the clock never jumps. */
static DWORD g_tc_base;
typedef struct { LONGLONG q0; double ms0, rate; } clk_anchor;
static clk_anchor g_clk[2];
static volatile LONG g_clk_i;

static double clk_ms(const clk_anchor *a, LONGLONG q)
{
    return a->ms0 + (double)(q - a->q0) * 1000.0 / (double)g_qpf.QuadPart * a->rate;
}

static DWORD WINAPI h_GetTickCount(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return g_tc_base + (DWORD)(LONGLONG)clk_ms(&g_clk[g_clk_i], now.QuadPart);
}

__declspec(dllexport) void nba98core_set_clock_rate(double rate)
{
    LONG i = g_clk_i;
    LARGE_INTEGER now;
    if (!g_stats.precise_clock) return;
    if (rate < 0.97) rate = 0.97;
    if (rate > 1.03) rate = 1.03;
    QueryPerformanceCounter(&now);
    g_clk[i ^ 1].ms0 = clk_ms(&g_clk[i], now.QuadPart);
    g_clk[i ^ 1].q0 = now.QuadPart;
    g_clk[i ^ 1].rate = rate;
    InterlockedExchange(&g_clk_i, i ^ 1);
    g_stats.clock_rate = (float)rate;
}

/*
 * Input frames (0.7.6). In a match the game runs one simulation step per input frame. Its timer
 * task 0x42E4A0 makes one every 2 ticks (4 in the CPU v CPU 30 Hz mode): it counts ticks in
 * 0x5C6034 and resets that counter to 0 when it samples the controllers. The task is
 * registered at 0x42E0F5 (mov eax, 0x42E4A0; jmp 0x525548) and removed at 0x42E101 (same
 * pointer); both immediates point at our wrapper instead, which runs the task and counts a
 * frame when it left 0x5C6034 at 0 (a ring-full or stopped sample leaves it above 0). It runs
 * inside the timer callback, so input_frames is up to date when ticks_done moves and
 * tick_event is set. glide2x's step lock waits for this count instead of raw ticks: counting
 * ticks, it started from whatever tick was current and half the time released on the tick
 * between input frames, which with the display lock at mid-refresh put the frame-making tick
 * on the vblank and gave long runs of frames with 0 or 2 steps (Jason's 0.6.9 logs).
 */
volatile long *g_input_frames_p __asm__("nba98_input_frames_p") = &g_stats.input_frames;
uint32_t g_input_task __asm__("nba98_input_task") = 0x42E4A0;
__asm__(".text\n"
        "nba98_input_task_wrap:\n"
        "  call *nba98_input_task\n"
        "  pushfl\n"
        "  cmpl $0, 0x5C6034\n"
        "  jne 1f\n"
        "  pushl %eax\n"
        "  movl nba98_input_frames_p, %eax\n"
        "  lock incl (%eax)\n"
        "  popl %eax\n"
        "1:\n"
        "  popfl\n"
        "  ret\n");
void input_task_wrap(void) __asm__("nba98_input_task_wrap");

static void input_frames_setup(void)
{
    static const unsigned char e1[5] = {0xB8, 0xA0, 0xE4, 0x42, 0x00};   /* mov eax, 0x42E4A0 at 0x42E0F5 */
    unsigned char m[5];
    uint32_t p = (uint32_t)(uintptr_t)input_task_wrap;
    DWORD old;
    if (!g_is_v11 || memcmp((const void *)(uintptr_t)0x42E0F5, e1, 5) || memcmp((const void *)(uintptr_t)0x42E101, e1, 5)) {
        LOG("step lock: input frame counter not added (exe code differs); glide2x counts ticks");
        return;
    }
    m[0] = 0xB8; memcpy(m + 1, &p, 4);
    if (!VirtualProtect((void *)(uintptr_t)0x42E0F5, 0x12, PAGE_EXECUTE_READWRITE, &old)) return;
    memcpy((void *)(uintptr_t)0x42E0F5, m, 5);
    memcpy((void *)(uintptr_t)0x42E101, m, 5);
    VirtualProtect((void *)(uintptr_t)0x42E0F5, 0x12, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)0x42E0F5, 0x12);
    LOG("step lock: input frame counter on (input task 0x42E4A0 wrapped)");
}

/* called from timer_thunk on the timer thread, after the game's callback */
static void timing_after_timer(LPTIMECALLBACK cb)
{
    if (cb != g_clock_cb) return;
    InterlockedIncrement(&g_stats.ticks_done);
    if (g_stats.tick_event) SetEvent((HANDLE)g_stats.tick_event);
}

/* called from timer_thunk on the timer thread, before the game's callback */
static void timing_on_timer(LPTIMECALLBACK cb)
{
    LARGE_INTEGER now;
    if (!g_clock_cb) g_clock_cb = cb;
    if (cb != g_clock_cb) return;
    QueryPerformanceCounter(&now);
    InterlockedIncrement(&g_stats.ticks);
    if (g_tick_last.QuadPart) {
        double gap = (double)(now.QuadPart - g_tick_last.QuadPart) * 1000.0 / (double)g_qpf.QuadPart;
        if (gap < g_gap_min) g_gap_min = gap;
        if (gap > g_gap_max) g_gap_max = gap;
        g_gap_sum += gap;
        g_gap_n++;
    }
    g_tick_last = now;
}

static void tlog(const char *fmt, ...)
{
    char buf[512];
    int n;
    va_list ap;
    DWORD w;
    if (g_tlog == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    n = _vsnprintf(buf, sizeof buf - 3, fmt, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof buf - 3) n = (int)sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';
    WriteFile(g_tlog, buf, (DWORD)n, &w, NULL);
}

static void lod_report(char *out, size_t cap);   /* player_lod.inc */

static DWORD WINAPI timing_thread(LPVOID arg)
{
    LARGE_INTEGER t0, t1;
    long ticks0 = g_stats.ticks, steps0 = g_stats.sim_steps, inf0 = g_stats.input_frames, r_seq_logged = 0, quiet = 0;
    (void)arg;
    sys_report();   /* machine report: here, not in DllMain (it loads dxgi.dll) */
    if (g_tlog != INVALID_HANDLE_VALUE) {
        tlog("NBA Live 98 timing log, %s. One line per second.", NBA98_VERSION_LINE);
        tlog("machine: %s", g_machine);
        tlog("ticks: game clock callbacks (target 120/s) and the gap between them (min avg max).");
        tlog("steps: game simulation steps (60/s while a match runs, 0 in menus); inp: input frames the game made (the step lock counts these).");
        tlog("frames: frames shown, frame time (min avg max), how many new steps each frame showed, frames over 50 ms.");
        tlog("uniforms (in a match): players on each uniform texture size (distance detail), and the 256 slots the game was granted.");
    }
    QueryPerformanceCounter(&t0);
    for (;;) {
        double len, gmin, gmax, gsum;
        long gn, ticks, steps, inf;
        Sleep(1000);
        {
            static int secs;
            if (++secs % 60 == 0) { fscache_report(); tempbuf_report(); spchmem_report(); }
        }
        QueryPerformanceCounter(&t1);
        len = (double)(t1.QuadPart - t0.QuadPart) / (double)g_qpf.QuadPart;
        t0 = t1;
        /* the timer thread is the only writer; a sample torn by one tick does not matter here */
        gmin = g_gap_min; gmax = g_gap_max; gsum = g_gap_sum; gn = g_gap_n;
        g_gap_min = 1e9; g_gap_max = 0; g_gap_sum = 0; g_gap_n = 0;
        ticks = g_stats.ticks; steps = g_stats.sim_steps;
        g_stats.sec_len_s = (float)len;
        g_stats.sec_ticks = (float)((ticks - ticks0) / len);
        g_stats.sec_steps = (float)((steps - steps0) / len);
        g_stats.sec_gap_min = gn ? (float)gmin : 0;
        g_stats.sec_gap_avg = gn ? (float)(gsum / gn) : 0;
        g_stats.sec_gap_max = gn ? (float)gmax : 0;
        InterlockedIncrement(&g_stats.sec_seq);
        ticks0 = ticks; steps0 = steps;
        inf = g_stats.input_frames;
        if (g_tlog != INVALID_HANDLE_VALUE) {
            char fr[400] = "frames: no data";
            /* glide2x publishes on its own one-second windows, which drift against ours: a
               second without a new publish is only a stall if the one before had none either */
            if (g_stats.r_seq != r_seq_logged || (g_stats.r_seq && !quiet++)) {
                if (g_stats.r_seq != r_seq_logged) quiet = 0;
                r_seq_logged = g_stats.r_seq;
                _snprintf(fr, sizeof fr, "frames %5.1f/s  frame ms %5.1f %5.1f %5.1f  steps/frame 0:%-3ld 1:%-3ld 2:%-3ld 3+:%-3ld  >50ms %ld",
                          g_stats.r_fps, g_stats.r_ft_min, g_stats.r_ft_avg, g_stats.r_ft_max, g_stats.r_spf[0],
                          g_stats.r_spf[1], g_stats.r_spf[2], g_stats.r_spf[3], g_stats.r_long);
            } else if (g_stats.r_seq) {
                _snprintf(fr, sizeof fr, "frames: none this second (stall or loading)");
            }
            if (g_stats.disp_lock) {
                size_t l = strlen(fr);
                _snprintf(fr + l, sizeof fr - l, "  |  display %.3f Hz, present at %.2f %.2f %.2f of refresh, clock x%.5f",
                          g_stats.disp_hz, g_stats.r_ph_min, g_stats.r_ph_avg, g_stats.r_ph_max, g_stats.clock_rate);
            }
            if (g_stats.sec_steps > 0) {   /* in a match: distance detail tiers */
                size_t l = strlen(fr);
                lod_report(fr + l, sizeof fr - l);
            }
            tlog("%8.1f s  ticks %5.1f/s  gap ms %5.1f %5.1f %5.1f  steps %5.1f/s  inp %5.1f/s  |  %s", now_ms() / 1000.0,
                 g_stats.sec_ticks, g_stats.sec_gap_min, g_stats.sec_gap_avg, g_stats.sec_gap_max, g_stats.sec_steps,
                 (inf - inf0) / len, fr);
        }
        inf0 = inf;
    }
    return 0;
}

static void timing_setup(void)
{
    HANDLE h;
    g_stats.crt_mode = -1;   /* set by the options menu when the CRT filter is changed */
    g_stats.tick_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (ini_int("PreciseClock", 1)) {
        LARGE_INTEGER q;
        g_tc_base = GetTickCount();
        QueryPerformanceCounter(&q);
        g_clk[0].q0 = q.QuadPart;
        g_clk[0].ms0 = 0;
        g_clk[0].rate = 1.0;
        g_stats.clock_rate = 1.0f;
        if (iat_hook(GetModuleHandleA(NULL), "KERNEL32.dll", "GetTickCount", (void *)h_GetTickCount)) {
            g_stats.precise_clock = 1;
            timeBeginPeriod(1);
            LOG("timing: precise clock on (game GetTickCount from QueryPerformanceCounter, timer resolution 1 ms)");
        } else {
            LOG("timing: could not hook GetTickCount; game clock left as is");
        }
    }
    if (g_is_v11) {
        uint8_t *p = (uint8_t *)STEP_PATCH_VA;
        DWORD old;
        static const uint8_t expect[5] = { 0xA1, 0xD1, 0x7E, 0x5E, 0x00 };
        if (!memcmp(p, expect, 5) && VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) {
            int32_t rel = (int32_t)((uintptr_t)step_thunk - (STEP_PATCH_VA + 5));
            p[0] = 0xE8;
            memcpy(p + 1, &rel, 4);
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            LOG("timing: counting simulation steps at 0x%08x", STEP_PATCH_VA);
        } else {
            LOG("timing: simulation step code not as expected; steps not counted");
        }
    }
    if (ini_int("TimingLog", NBA98_DEBUG)) {
        char path[MAX_PATH];
        nba98_log_path(g_exe_dir, "timing.log", path, sizeof path);
        g_tlog = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    h = o_CreateThread(NULL, 0, timing_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
}
