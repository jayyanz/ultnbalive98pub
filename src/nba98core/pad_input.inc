/*
 * Gamepads for NBA Live 98 through SDL3 (included by nba98core.cpp).
 *
 * The game reads joysticks through WINMM (joyGetNumDevs, joyGetDevCapsA,
 * joyGetPosEx) from a polling thread (0x4F3174) that checks joysticks 0..3.
 * Its controller bindings (saves\ctrl98.dat, 4 records of 0x6D bytes, 20
 * bindings each from offset 0x1D) are dwords: top nibble = device (1 =
 * keyboard, 2..5 = joystick 1..4), low bits = key scan code, or for a
 * joystick a button bit mask (bit n = button n+1) or a direction
 * (0x10000 up: Y < 0x6000, 0x20000 down: Y > 0xA000, 0x40000 left,
 * 0x80000 right on X). Record byte 0x1C is the record's device (the same
 * nibble); the game's check (0x4F3E74) rejects the whole file, and rebuilds
 * every controller from its defaults, if a binding names another device or
 * a missing one.
 *
 * Movement is digital in the game (8 directions, see 0x4F3314); there is no
 * analogue path. [input] Stick=8way (default) turns the left stick into a
 * clean 8-way direction (45 degree sectors) so diagonals behave like a D-pad.
 *
 * We replace those three WINMM calls with SDL3 gamepads, so any controller
 * SDL knows (Xbox, PlayStation, Switch, generic) works, with triggers as
 * buttons and the D-pad and left stick on X/Y. Button numbers follow the
 * Windows XInput joystick order, so configs made with an Xbox pad keep
 * working:
 *   1 Cross/A   2 Circle/B   3 Square/X   4 Triangle/Y   5 L1/LB   6 R1/RB
 *   7 Select/Back   8 Start   9 L3   10 R3   11 L2/LT   12 R2/RT
 *
 * Defaults: the game builds a controller's default bindings (0x4F40C8) from
 * templates at 0x5CA2DD (8 records of 0x6D bytes), picking the joystick
 * template by button count (4, 6, 8, 10 buttons; anything else gets the
 * sparse template 0). [input] PadLayout=ps1 (default) replaces the joystick
 * templates with the PlayStation NBA Live 98 layout, so the game's own
 * defaults are the PS1 layout: on a first run, after a reset, and when the
 * game re-defaults controllers that were never customised. At start-up,
 * records in saves\ctrl98.dat that still hold untouched factory defaults are
 * moved to the PS1 layout (on gamepad N for controller N when that pad is
 * connected); customised records are left alone. The first change keeps the
 * original as ctrl98.dat.bak.
 */

typedef uint32_t SDL_JoystickID;
typedef struct SDL_Gamepad SDL_Gamepad;
#define SDL_INIT_GAMEPAD 0x00002000u

static struct {
    HMODULE dll;
    bool (__cdecl *Init)(uint32_t);
    bool (__cdecl *SetHint)(const char *, const char *);
    const char *(__cdecl *GetError)(void);
    SDL_JoystickID *(__cdecl *GetGamepads)(int *);
    SDL_Gamepad *(__cdecl *OpenGamepad)(SDL_JoystickID);
    void (__cdecl *CloseGamepad)(SDL_Gamepad *);
    bool (__cdecl *GamepadConnected)(SDL_Gamepad *);
    bool (__cdecl *GetGamepadButton)(SDL_Gamepad *, int);
    int16_t (__cdecl *GetGamepadAxis)(SDL_Gamepad *, int);
    void (__cdecl *UpdateGamepads)(void);
    const char *(__cdecl *GetGamepadName)(SDL_Gamepad *);
    void (__cdecl *free)(void *);
    void (__cdecl *FlushEvents)(uint32_t, uint32_t);
} psdl;

#if NBA98_DEBUG
/* test mode (developer only): NBA98_TEST_PAD=<file> attaches an SDL virtual
 * gamepad whose state is read from that file: "<SDL button mask hex> <left x> <left y>" */
typedef struct {
    uint32_t version; uint16_t type, padding, vendor_id, product_id, naxes, nbuttons, nballs, nhats,
        ntouchpads, nsensors, padding2[2]; uint32_t button_mask, axis_mask; const char *name;
    const void *touchpads, *sensors; void *userdata; void *cb[8];
} VJoyDesc;
static struct {
    SDL_JoystickID (__cdecl *Attach)(const VJoyDesc *);
    void *(__cdecl *OpenJoystick)(SDL_JoystickID);
    bool (__cdecl *SetButton)(void *, int, bool);
    bool (__cdecl *SetAxis)(void *, int, int16_t);
    void *joy;
    char file[MAX_PATH];
    FILETIME ft;
} vpad;

static void vpad_init(void)
{
    VJoyDesc d;
    SDL_JoystickID id;
    if (!GetEnvironmentVariableA("NBA98_TEST_PAD", vpad.file, sizeof vpad.file)) return;
    *(FARPROC *)&vpad.Attach = GetProcAddress(psdl.dll, "SDL_AttachVirtualJoystick");
    *(FARPROC *)&vpad.OpenJoystick = GetProcAddress(psdl.dll, "SDL_OpenJoystick");
    *(FARPROC *)&vpad.SetButton = GetProcAddress(psdl.dll, "SDL_SetJoystickVirtualButton");
    *(FARPROC *)&vpad.SetAxis = GetProcAddress(psdl.dll, "SDL_SetJoystickVirtualAxis");
    if (!vpad.Attach || !vpad.OpenJoystick || !vpad.SetButton || !vpad.SetAxis) return;
    memset(&d, 0, sizeof d);
    d.version = sizeof d;
    d.type = 1;               /* gamepad */
    d.naxes = 6;
    d.nbuttons = 26;
    d.name = "Test Pad";
    id = vpad.Attach(&d);
    vpad.joy = id ? vpad.OpenJoystick(id) : NULL;
    LOG("gamepads: test pad %s (state from %s)", vpad.joy ? "attached" : "FAILED", vpad.file);
}

static void vpad_poll(void)
{
    WIN32_FILE_ATTRIBUTE_DATA a;
    char buf[64];
    DWORD got = 0;
    HANDLE f;
    unsigned long mask = 0;
    long x = 0, y = 0;
    int i;
    if (!vpad.joy || !GetFileAttributesExA(vpad.file, GetFileExInfoStandard, &a)) return;
    if (!CompareFileTime(&a.ftLastWriteTime, &vpad.ft)) return;
    vpad.ft = a.ftLastWriteTime;
    f = CreateFileA(vpad.file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    if (sscanf(buf, "%lx %ld %ld", &mask, &x, &y) < 1) return;
    for (i = 0; i < 26; i++) vpad.SetButton(vpad.joy, i, (mask >> i) & 1);
    vpad.SetAxis(vpad.joy, 0, (int16_t)x);
    vpad.SetAxis(vpad.joy, 1, (int16_t)y);
    LOG("gamepads: test pad state 0x%lx %ld %ld", mask, x, y);
}

#endif

#define PAD_SLOTS 4
static struct { SDL_JoystickID id; SDL_Gamepad *gp; } g_pad[PAD_SLOTS];
static int g_pad_on, g_pad_ready, g_pad_failed, g_pad_deadzone, g_pad_8way, g_pad_ps1, g_pad_first_call = 1;
static void pad_upgrade_ctrl98(void);
static CRITICAL_SECTION g_pad_cs;
static DWORD g_pad_last_update;

typedef UINT (WINAPI *joyGetNumDevs_fn)(void);
typedef MMRESULT (WINAPI *joyGetDevCapsA_fn)(UINT_PTR, LPJOYCAPSA, UINT);
typedef MMRESULT (WINAPI *joyGetPosEx_fn)(UINT, LPJOYINFOEX);
static joyGetNumDevs_fn o_joyGetNumDevs;
static joyGetDevCapsA_fn o_joyGetDevCapsA;
static joyGetPosEx_fn o_joyGetPosEx;

static int pad_load(void)
{
    char path[MAX_PATH];
    if (g_pad_ready) return 1;
    if (g_pad_failed) return 0;
    g_pad_failed = 1;
    _snprintf(path, sizeof path, "%sSDL3.dll", g_exe_dir);
    psdl.dll = LoadLibraryA(path);
    if (!psdl.dll) { LOG("gamepads: SDL3.dll not found next to the game"); return 0; }
#define G(n) *(FARPROC *)&psdl.n = GetProcAddress(psdl.dll, "SDL_" #n); if (!psdl.n) { LOG("gamepads: SDL3.dll has no SDL_" #n); return 0; }
    G(Init) G(SetHint) G(GetError) G(GetGamepads) G(OpenGamepad) G(CloseGamepad) G(GamepadConnected)
    G(GetGamepadButton) G(GetGamepadAxis) G(UpdateGamepads) G(GetGamepadName) G(free) G(FlushEvents)
#undef G
    psdl.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    if (!psdl.Init(SDL_INIT_GAMEPAD)) { LOG("gamepads: SDL init failed: %s", psdl.GetError()); return 0; }
    g_pad_failed = 0;
    g_pad_ready = 1;
#if NBA98_DEBUG
    vpad_init();
#endif
    LOG("gamepads: SDL3 gamepad support on (4 joystick slots)");
    return 1;
}

/* open new gamepads into free slots, drop disconnected ones */
static void pad_refresh(void)
{
    DWORD now = GetTickCount();
    int i, j, n = 0;
    SDL_JoystickID *ids;
    if (now - g_pad_last_update < 2) return;
    g_pad_last_update = now;
#if NBA98_DEBUG
    vpad_poll();
#endif
    psdl.UpdateGamepads();
    psdl.FlushEvents(0, 0xFFFFFFFFu);   /* nobody reads SDL's event queue */
    for (i = 0; i < PAD_SLOTS; i++)
        if (g_pad[i].gp && !psdl.GamepadConnected(g_pad[i].gp)) {
            LOG("gamepads: joystick %d disconnected", i + 1);
            psdl.CloseGamepad(g_pad[i].gp);
            g_pad[i].gp = NULL;
            g_pad[i].id = 0;
        }
    ids = psdl.GetGamepads(&n);
    for (j = 0; ids && j < n; j++) {
        int have = 0, free_slot = -1;
        for (i = 0; i < PAD_SLOTS; i++) {
            if (g_pad[i].gp && g_pad[i].id == ids[j]) have = 1;
            if (!g_pad[i].gp && free_slot < 0) free_slot = i;
        }
        if (!have && free_slot >= 0) {
            SDL_Gamepad *gp = psdl.OpenGamepad(ids[j]);
            if (gp) {
                g_pad[free_slot].gp = gp;
                g_pad[free_slot].id = ids[j];
                LOG("gamepads: \"%s\" is joystick %d", psdl.GetGamepadName(gp) ? psdl.GetGamepadName(gp) : "?",
                    free_slot + 1);
            }
        }
    }
    if (ids) psdl.free(ids);
}

/* The game counts joysticks (0x4F33C8) just before it loads ctrl98.dat, so
   that first call is where SDL starts and factory records are upgraded
   (not in DllMain, which runs under the loader lock). */
static UINT WINAPI h_joyGetNumDevs(void)
{
    if (!g_pad_on) return o_joyGetNumDevs();
    EnterCriticalSection(&g_pad_cs);
    if (g_pad_first_call) {
        g_pad_first_call = 0;
        if (pad_load()) {
            DWORD t0 = GetTickCount(), seen = 0;
            int i, n;
            /* give SDL a moment to report pads that are already plugged in */
            for (;;) {
                g_pad_last_update = GetTickCount() - 10;
                pad_refresh();
                for (n = 0, i = 0; i < PAD_SLOTS; i++) n += g_pad[i].gp != NULL;
                if (n && !seen) seen = GetTickCount();
                if (seen ? GetTickCount() - seen > 100 : GetTickCount() - t0 > 500) break;
                Sleep(10);
            }
            LOG("gamepads: %d connected at start-up", n);
        }
        if (g_pad_ps1) pad_upgrade_ctrl98();
    }
    LeaveCriticalSection(&g_pad_cs);
    return PAD_SLOTS;
}

static MMRESULT WINAPI h_joyGetDevCapsA(UINT_PTR id, LPJOYCAPSA caps, UINT size)
{
    MMRESULT r = JOYERR_UNPLUGGED;
    if (!g_pad_on) return o_joyGetDevCapsA(id, caps, size);
    EnterCriticalSection(&g_pad_cs);
    if (pad_load() && id < PAD_SLOTS) {
        pad_refresh();
        if (g_pad[id].gp && caps && size >= sizeof(JOYCAPSA)) {
            memset(caps, 0, size);
            caps->wMid = 0xFFFF;
            caps->wPid = 0xFFFF;
            _snprintf(caps->szPname, sizeof caps->szPname, "Gamepad %u", (unsigned)id + 1);
            caps->wXmax = caps->wYmax = caps->wZmax = caps->wRmax = caps->wUmax = caps->wVmax = 65535;
            caps->wNumButtons = 12;
            caps->wPeriodMin = 1;
            caps->wPeriodMax = 1000;
            caps->wMaxAxes = caps->wNumAxes = 2;
            caps->wMaxButtons = 12;
            r = JOYERR_NOERROR;
        }
    }
    LeaveCriticalSection(&g_pad_cs);
    return r;
}

static DWORD axis_to_winmm(int16_t v)
{
    int a = v;
    if (a > -g_pad_deadzone && a < g_pad_deadzone) a = 0;
    return (DWORD)((a + 32768) & 0xFFFF);
}

/* left stick -> one of 8 directions (45 degree sectors), centre inside the
   dead zone; tan(22.5 degrees) is about 53/128 */
static void stick_8way(int16_t sx, int16_t sy, DWORD *x, DWORD *y)
{
    int a = sx, b = sy, aa = a < 0 ? -a : a, ab = b < 0 ? -b : b;
    *x = *y = 32767;
    if ((long long)a * a + (long long)b * b < (long long)g_pad_deadzone * g_pad_deadzone) return;
    if (ab * 128 > aa * 53) *y = b < 0 ? 0 : 65535;
    if (aa * 128 > ab * 53) *x = a < 0 ? 0 : 65535;
}

static MMRESULT WINAPI h_joyGetPosEx(UINT id, LPJOYINFOEX ji)
{
    MMRESULT r = JOYERR_UNPLUGGED;
    if (!g_pad_on) return o_joyGetPosEx(id, ji);
    EnterCriticalSection(&g_pad_cs);
    if (pad_load() && id < PAD_SLOTS && ji) {
        SDL_Gamepad *gp;
        pad_refresh();
        gp = g_pad[id].gp;
        if (gp) {
            static const int order[10] = { 0 /*South*/, 1 /*East*/, 2 /*West*/, 3 /*North*/, 9 /*L1*/, 10 /*R1*/,
                                           4 /*Back*/, 6 /*Start*/, 7 /*L3*/, 8 /*R3*/ };
            DWORD b = 0, x, y;
            int i;
            for (i = 0; i < 10; i++) if (psdl.GetGamepadButton(gp, order[i])) b |= 1u << i;
            if (psdl.GetGamepadAxis(gp, 4) > 12000) b |= 1u << 10;   /* L2 trigger */
            if (psdl.GetGamepadAxis(gp, 5) > 12000) b |= 1u << 11;   /* R2 trigger */
            if (g_pad_8way) stick_8way(psdl.GetGamepadAxis(gp, 0), psdl.GetGamepadAxis(gp, 1), &x, &y);
            else { x = axis_to_winmm(psdl.GetGamepadAxis(gp, 0)); y = axis_to_winmm(psdl.GetGamepadAxis(gp, 1)); }
            if (psdl.GetGamepadButton(gp, 13)) x = 0;          /* D-pad left */
            if (psdl.GetGamepadButton(gp, 14)) x = 65535;      /* D-pad right */
            if (psdl.GetGamepadButton(gp, 11)) y = 0;          /* D-pad up */
            if (psdl.GetGamepadButton(gp, 12)) y = 65535;      /* D-pad down */
            {
                DWORD size = ji->dwSize;
                memset(ji, 0, size >= sizeof *ji ? sizeof *ji : size);
                ji->dwSize = size;
            }
            ji->dwXpos = x;
            ji->dwYpos = y;
            ji->dwZpos = ji->dwRpos = ji->dwUpos = ji->dwVpos = 32767;
            ji->dwButtons = b;
            for (i = 0; i < 32; i++) if (b & (1u << i)) ji->dwButtonNumber++;
            ji->dwPOV = JOY_POVCENTERED;
            r = JOYERR_NOERROR;
        }
    }
    LeaveCriticalSection(&g_pad_cs);
    return r;
}

/* PlayStation NBA Live 98 defaults (manual): Circle shoot, Cross pass,
 * Triangle spin (pivot), Square crossover; defence Square steal, Triangle
 * jump, Cross switch, Circle hand-check; R1 turbo, L1 direct pass/switch,
 * L2 direct shoot (dunking) / last man back, R2 drive / face up,
 * Select play calling, Start pause. */
static void pad_ps1_bindings(uint32_t *b, int joy)
{
    uint32_t t = (uint32_t)(2 + joy) << 28;
    enum { CROSS = 0x1, CIRCLE = 0x2, SQUARE = 0x4, TRIANGLE = 0x8, L1 = 0x10, R1 = 0x20,
           SELECT = 0x40, START = 0x80, L2 = 0x400, R2 = 0x800 };
    b[0] = t | 0x10000;  b[1] = t | 0x20000;  b[2] = t | 0x40000;  b[3] = t | 0x80000;   /* up down left right */
    b[4] = t | R1;       /* turbo */
    b[5] = t | SELECT;   /* play calling */
    b[6] = t | L1;       /* direct pass / switch */
    b[7] = t | START;    /* pause */
    b[8] = t | CIRCLE;   /* shoot */
    b[9] = t | CROSS;    /* pass */
    b[10] = t | TRIANGLE;/* pivot (spin / back down) */
    b[11] = t | L2;      /* dunking (direct shoot) */
    b[12] = t | R2;      /* drive */
    b[13] = t | SQUARE;  /* crossover */
    b[14] = t | SQUARE;  /* steal */
    b[15] = t | TRIANGLE;/* jump */
    b[16] = t | CROSS;   /* switch */
    b[17] = t | CIRCLE;  /* hand-check */
    b[18] = t | L2;      /* last man back */
    b[19] = t | R2;      /* face up */
}

#define TMPL_VA 0x5CA2DD          /* default templates: 8 records of 0x6D bytes */
#define TMPL_COUNT 8
static uint32_t g_tmpl_orig[TMPL_COUNT][20];
static int g_tmpl_ok;

/* the game's own defaults become the PS1 layout (v1.1 exe only) */
static void pad_patch_defaults(void)
{
    uint8_t *t = (uint8_t *)TMPL_VA;
    uint32_t ps1[20], first;
    DWORD old;
    int i, n = 0;
    if (!g_is_v11) { LOG("gamepads: not the v1.1 exe, game defaults left as they are"); return; }
    memcpy(&first, t + 5 * 0x6D + 0x1D, 4);
    if (first != 0x10000048u || t[4 * 0x6D + 0x1C] != 2) { LOG("gamepads: default templates not where expected, left as they are"); return; }
    for (i = 0; i < TMPL_COUNT; i++) memcpy(g_tmpl_orig[i], t + i * 0x6D + 0x1D, 80);
    g_tmpl_ok = 1;
    if (!g_pad_ps1) return;
    pad_ps1_bindings(ps1, 0);
    if (!VirtualProtect(t, TMPL_COUNT * 0x6D, PAGE_READWRITE, &old)) return;
    for (i = 0; i < TMPL_COUNT; i++)
        if (t[i * 0x6D + 0x1C] == 2) { memcpy(t + i * 0x6D + 0x1D, ps1, 80); n++; }
    VirtualProtect(t, TMPL_COUNT * 0x6D, old, &old);
    LOG("gamepads: game's joystick defaults set to the PS1 layout (%d templates)", n);
}

/* does this record hold what the game's default builder makes from template
   tm for the record's device? (the builder swaps in the device nibble and
   drops unbound entries) */
static int rec_is_template(const uint8_t *rec, const uint32_t *tm)
{
    uint32_t d = rec[0x1C], b;
    int i;
    for (i = 0; i < 20; i++) {
        memcpy(&b, rec + 0x1D + 4 * i, 4);
        if ((tm[i] >> 28) == 0 ? b != 0 : b != ((d << 28) | (tm[i] & 0x0FFFFFFFu))) return 0;
    }
    return 1;
}

static int rec_is_factory(const uint8_t *rec)
{
    uint32_t ps1[20];
    int i;
    pad_ps1_bindings(ps1, 0);
    if (rec_is_template(rec, ps1)) return 1;
    for (i = 0; i < TMPL_COUNT; i++) if (rec_is_template(rec, g_tmpl_orig[i])) return 1;
    return 0;
}

static void pad_upgrade_ctrl98(void)
{
    char path[MAX_PATH], bak[MAX_PATH];
    uint8_t d[0x6D * 4], before[0x6D * 4];
    DWORD got = 0, w;
    HANDLE f;
    int r;
    if (!g_tmpl_ok) return;
    _snprintf(path, sizeof path, "%s%s\\ctrl98.dat", g_exe_dir, roster_saves_dir());
    _snprintf(bak, sizeof bak, "%s%s\\ctrl98.dat.bak", g_exe_dir, roster_saves_dir());
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { LOG("gamepads: no saves\\ctrl98.dat yet; the game will build PS1 defaults"); return; }
    ReadFile(f, d, sizeof d, &got, NULL);
    CloseHandle(f);
    if (got != sizeof d) { LOG("gamepads: saves\\ctrl98.dat has an unexpected size (%lu), left alone", got); return; }
    memcpy(before, d, sizeof d);
    for (r = 0; r < 4; r++) {
        uint8_t *rec = d + r * 0x6D;
        int dev = rec[0x1C], joy = -1;
        uint32_t b[20];
        if (dev < 1 || dev > 5 || !rec_is_factory(rec)) {
            LOG("gamepads: controller %d has custom bindings, left as they are", r + 1);
            continue;
        }
        if (g_pad[r].gp) joy = r;                      /* the game's default: pad N on controller N */
        else if (dev >= 2) joy = dev - 2;              /* keep the pad it already uses */
        if (joy < 0) { LOG("gamepads: controller %d: keyboard defaults, no pad %d connected", r + 1, r + 1); continue; }
        pad_ps1_bindings(b, joy);
        memcpy(rec + 0x1D, b, sizeof b);
        rec[0x1C] = (uint8_t)(2 + joy);
        LOG("gamepads: controller %d: defaults -> PS1 layout on joystick %d", r + 1, joy + 1);
    }
    if (!memcmp(before, d, sizeof d)) return;
    if (GetFileAttributesA(bak) == INVALID_FILE_ATTRIBUTES) CopyFileA(path, bak, TRUE);
    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { LOG("gamepads: could not write ctrl98.dat"); return; }
    WriteFile(f, d, sizeof d, &w, NULL);
    CloseHandle(f);
    LOG("gamepads: saves\\ctrl98.dat updated to the PS1 layout");
}

/* Deadzone and Stick: read at start-up and again when changed on the Controls page (0.7.3);
   the pad code reads both on every poll, so a new value applies at once */
static void pad_reload_live(void)
{
    char v[16];
    int dz = (int)GetPrivateProfileIntA("input", "Deadzone", 20, g_ini);
    if (dz < 0) dz = 0;
    if (dz > 90) dz = 90;
    g_pad_deadzone = 32768 * dz / 100;
    GetPrivateProfileStringA("input", "Stick", "8way", v, sizeof v, g_ini);
    g_pad_8way = lstrcmpiA(v, "raw") != 0;
    LOG("gamepads: dead zone %d%%, stick %s", dz, g_pad_8way ? "8-way" : "raw");
}

static void pad_setup(HMODULE exe)
{
    g_pad_on = (int)GetPrivateProfileIntA("input", "Gamepads", 1, g_ini);
    pad_reload_live();
    {
        char v[16];
        GetPrivateProfileStringA("input", "PadLayout", "ps1", v, sizeof v, g_ini);
        g_pad_ps1 = lstrcmpiA(v, "ps1") == 0;
    }
    if (!g_pad_on) { LOG("gamepads: off (Windows joysticks used directly)"); return; }
    InitializeCriticalSection(&g_pad_cs);
    o_joyGetNumDevs = (joyGetNumDevs_fn)iat_hook(exe, "WINMM.dll", "joyGetNumDevs", (void *)h_joyGetNumDevs);
    o_joyGetDevCapsA = (joyGetDevCapsA_fn)iat_hook(exe, "WINMM.dll", "joyGetDevCapsA", (void *)h_joyGetDevCapsA);
    o_joyGetPosEx = (joyGetPosEx_fn)iat_hook(exe, "WINMM.dll", "joyGetPosEx", (void *)h_joyGetPosEx);
    if (!o_joyGetNumDevs) o_joyGetNumDevs = joyGetNumDevs;
    if (!o_joyGetDevCapsA) o_joyGetDevCapsA = joyGetDevCapsA;
    if (!o_joyGetPosEx) o_joyGetPosEx = joyGetPosEx;
    pad_patch_defaults();
}
