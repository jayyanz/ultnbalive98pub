/*
 * Gamepad extras (0.7.12; included by nba98core.cpp after pad_menu.inc).
 *
 * - Intros: Start, A, X or B skips them as a key does on the keyboard (an Esc press). The
 *   intros are everything before the first menu screen (the UI event poll 0x4585E4 not yet
 *   called). The key goes in with keybd_event, only while the game's window is in front, so
 *   it reaches the game exactly as a keyboard key would.
 * - Automatic replays: Start, A, X or B skips the replay as Esc does on the keyboard. The
 *   auto replay loop in 0x426DA8 (taken when the replay is automatic, 0x5E628A high word set)
 *   runs a match frame (call 0x42CF14 at 0x426EAF) and then, if Esc is held (key table byte
 *   0x5D7015), waits for it to be let go and ends the replay. We mark that call, and while it
 *   ran in the last quarter second a fresh press holds Esc in the key table for 150 ms. The
 *   Instant Replay screen (0x42645C) and the pause menu are not touched.
 * - Right stick button (R3): CRT filter Off / Auto, saved to nba98.ini ([glide] CRT), applied
 *   at once (stats crt_mode), anywhere.
 * A press only counts once the buttons were let go (held buttons from play never fire it).
 * A thread reads the pads 60 times a second. [input] PadExtras=1 (default).
 */
#define PE_SKIP_BUTTONS ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 6))   /* SDL South (A), East (B), West (X), Start */
#define PE_CRT_BUTTON (1u << 8)                                            /* SDL right stick */

static volatile DWORD g_pe_replay_tick;   /* GetTickCount() of the last auto replay frame (0 = none yet) */
static int g_pe_replay_hooked;

void pe_replay_mark(void) __asm__("nba98_pe_replay_mark");
void pe_replay_mark(void) { DWORD t = GetTickCount(); g_pe_replay_tick = t ? t : 1; }

__asm__(".text\n"
        /* replaces "call 0x42CF14" at 0x426EAF (the auto replay loop's match frame) */
        "nba98_pe_replay_stub:\n"
        "  pushal\n"
        "  pushfl\n"
        "  cld\n"
        "  call nba98_pe_replay_mark\n"
        "  popfl\n"
        "  popal\n"
        "  pushl $0x42CF14\n"
        "  ret\n");
void pe_replay_stub(void) __asm__("nba98_pe_replay_stub");

static uint32_t pe_read(void)
{
    uint32_t m = 0;
    int i, b;
    if (!g_pad_on || !g_pad_ready) return 0;
    EnterCriticalSection(&g_pad_cs);
    pad_refresh();
    for (i = 0; i < PAD_SLOTS; i++) {
        if (!g_pad[i].gp) continue;
        for (b = 0; b < 9; b++) if (psdl.GetGamepadButton(g_pad[i].gp, b)) m |= 1u << b;
    }
    LeaveCriticalSection(&g_pad_cs);
    return m;
}

static int pe_game_in_front(void)
{
    DWORD pid = 0;
    HWND f = GetForegroundWindow();
    if (f) GetWindowThreadProcessId(f, &pid);
    return pid == GetCurrentProcessId();
}

static void pe_crt_toggle(void)
{
    int cur = g_stats.crt_mode >= 0 ? (int)g_stats.crt_mode : opt_crt();
    int m = cur ? 0 : 1;
    opt_ini_set("glide", "CRT", k_crt_ini[m]);
    g_stats.crt_mode = m;
    LOG("pad: right stick button, CRT filter %s", k_crt[m]);
}

static DWORD WINAPI pe_thread(LPVOID arg)
{
    uint32_t prev = 0xFFFFFFFFu;   /* everything counts as held until first let go */
    DWORD esc_until = 0;
    int esc_kind = 0;              /* 1 = key table (auto replay), 2 = keybd_event (intro) */
    (void)arg;
    for (;;) {
        DWORD now = GetTickCount();
        uint32_t held = pe_read(), press = held & ~prev;
        prev = held;
        if (esc_kind && (long)(now - esc_until) >= 0) {
            if (esc_kind == 1) pm_esc(0);
            else keybd_event(VK_ESCAPE, 0x01, KEYEVENTF_KEYUP, 0);
            esc_kind = 0;
        }
        {   /* log auto replays starting and ending (for the test logs) */
            static int in_replay;
            DWORD rt = g_pe_replay_tick;
            int r = rt && now - rt < 250;
            if (r != in_replay) { in_replay = r; LOG("pad: auto replay %s", r ? "running" : "over"); }
        }
        if (press & PE_CRT_BUTTON) pe_crt_toggle();
        if ((press & PE_SKIP_BUTTONS) && !esc_kind) {
            DWORD rt = g_pe_replay_tick;
            if (rt && now - rt < 250) {
                pm_esc(1);
                esc_kind = 1;
                esc_until = now + 150;
                LOG("pad: auto replay skipped (Esc)");
            } else if (!g_menus_seen && pe_game_in_front()) {
                keybd_event(VK_ESCAPE, 0x01, 0, 0);
                esc_kind = 2;
                esc_until = now + 100;
                LOG("pad: intro skipped (Esc)");
            }
        }
        Sleep(16);
    }
    return 0;
}

static void pad_extras_setup(void)
{
    static const unsigned char expect[5] = {0xE8, 0x60, 0x60, 0x00, 0x00};   /* call 0x42CF14 at 0x426EAF */
    unsigned char call[5];
    uint32_t rel;
    HANDLE t;
    if (!g_is_v11 || !g_pad_on || !g_pm_patched) { LOG("pad extras: off (needs the v1.1 exe, Gamepads=1 and the menu pad)"); return; }
    if (!GetPrivateProfileIntA("input", "PadExtras", 1, g_ini)) { LOG("pad extras: off (PadExtras=0)"); return; }
    call[0] = 0xE8;
    rel = (uint32_t)(uintptr_t)pe_replay_stub - (0x426EAF + 5);
    memcpy(call + 1, &rel, 4);
    g_pe_replay_hooked = opt_patch(0x426EAF, call, 5, expect);
    t = CreateThread(NULL, 0, pe_thread, NULL, 0, NULL);
    if (t) CloseHandle(t);
    LOG("pad extras: on (Start, A, X or B skips intros%s; right stick button switches the CRT filter Off / Auto)",
        g_pe_replay_hooked ? " and automatic replays" : "; auto replay hook FAILED (exe code differs)");
}
