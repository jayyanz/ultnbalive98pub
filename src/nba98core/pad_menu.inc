/*
 * Gamepad navigation of the front-end menus (0.7.4, roadmap item 13, decision D3).
 *
 * The game's menus are mouse driven. Every menu screen's run loop (and the Yes/No
 * pop-up, and our own pages) calls the UI event poll 0x4585E4 once per frame; we put a
 * jump at its entry (its first five bytes are five one-byte pushes) and run pad_menu_tick()
 * before it on the main thread.
 *
 * What the game gives us (NBAWIN.EXE v1.1):
 * - Widget table: 35 pointers at 0x5F9258. A live widget starts with the dword 'DOOG' and
 *   repeats it at +0x7C (the check at 0x452DD0). +0x1F word: id the poll returns when it is
 *   clicked (-1 = none). +0x1B/+0x1D words: position. +0x43: active. +0x45: radio group
 *   (0 none), +0x46: the chosen one in its group. +0x69 bit 8: never hovered. Size: +0x48 set: centred at the position, size
 *   words +0x53/+0x55; else the art at +0x25 gives the size (words +4/+6) and hot spot
 *   (words +8/+0xA), as in the hit test 0x456684.
 * - Raw mouse state, kept by the window procedure in 640x480 game pixels and read by the
 *   menus every frame (0x520418): x 0x62A30C, y 0x62A308, buttons 0x62A304 (bit 0 left).
 *   The menus then add the screen's x offset (0x5F389C >> 16) for the position they test.
 * - Esc as the game's own joystick menu code (0x48C454) presses it: key table byte 0x5D7015
 *   and last scan code 0x62A7CC (1 down, 0x81 up). The back arrow and the pop-ups' No
 *   button answer to it.
 *
 * What we do: the D-pad or left stick moves a selection between the live widgets (nearest in
 * the pressed direction); the selected widget gets the mouse position, so the game shows its
 * own hover look, and glide2x draws a dotted grey box round it (nav_box in the shared stats).
 * A (Cross) presses and releases the left mouse button there, so a click is exactly a mouse
 * click (held for at least three menu frames, so a slow frame cannot lose it). B (Circle) is Esc: back, or No on a pop-up. Moving the real mouse hides the box until
 * the pad is used again. Nothing happens while a match is running (the game clock's
 * simulation steps are moving), so in-game buttons never reach the menu code.
 *
 * The game has its own joystick menu code (0x48C454, called from the UI code while its flag
 * 0x5C7494 is set, which it is in the front end): a joystick controller's Shoot binding
 * presses Enter, Pass presses Esc and Pause a right click. With the PlayStation layout that
 * made Cross (Pass) go back and Circle (Shoot) go forward, the opposite of A and B here, so
 * while ours is on that code returns at once (jump at its entry, five one-byte pushes).
 *
 * [input] PadMenus=1 (default), also on the Controls page (applies at once).
 */

#define PM_SLOTS 35
#define PM_UP 1
#define PM_DOWN 2
#define PM_LEFT 4
#define PM_RIGHT 8
#define PM_A 16
#define PM_B 32
#define PM_DIRS (PM_UP | PM_DOWN | PM_LEFT | PM_RIGHT)

static volatile int g_pm_on;   /* [input] PadMenus, changed live by the Controls page */
static int g_pm_patched;
static const uint8_t *g_pm_sel;                    /* selected widget, NULL = none */
static int g_pm_held, g_pm_mouse_x = -1, g_pm_mouse_y = -1, g_pm_clicking, g_pm_escaping;
int g_pm_block_game_joy __asm__("nba98_pm_block_game_joy");   /* 1: the game's joystick menu code is skipped */
static DWORD g_pm_last_tick, g_pm_dir_time, g_pm_dir_next;
static long g_pm_steps;
static DWORD g_pm_steps_time;

typedef struct { const uint8_t *w; int x, y, cx, cy, wd, ht, id; } pm_obj;

static int16_t pm_w16(const uint8_t *p, int off) { int16_t v; memcpy(&v, p + off, 2); return v; }

static int pm_live(const uint8_t *w)
{
    uint32_t a, b;
    if (!w) return 0;
    memcpy(&a, w, 4);
    memcpy(&b, w + 0x7C, 4);
    return a == 0x474F4F44u && b == a;
}

/* the widgets a player can choose now, with their rectangles */
static int pm_collect(pm_obj *o)
{
    int i, n = 0;
    for (i = 0; i < PM_SLOTS; i++) {
        const uint8_t *w = *(const uint8_t *const *)(uintptr_t)(0x5F9258 + 4 * i);
        pm_obj t;
        if (!pm_live(w) || !w[0x43] || (w[0x69] & 8) || pm_w16(w, 0x1F) == -1) continue;
        if (w[0x48]) {
            t.wd = pm_w16(w, 0x53); t.ht = pm_w16(w, 0x55);
            t.x = pm_w16(w, 0x1B) - t.wd / 2; t.y = pm_w16(w, 0x1D) - t.ht / 2;
        } else {
            const uint8_t *art;
            memcpy(&art, w + 0x25, 4);
            if (!art) continue;
            t.wd = pm_w16(art, 4); t.ht = pm_w16(art, 6);
            t.x = pm_w16(w, 0x1B) - pm_w16(art, 8); t.y = pm_w16(w, 0x1D) - pm_w16(art, 0xA);
        }
        if (t.wd <= 0 || t.ht <= 0 || t.wd > 640 || t.ht > 480) continue;
        t.w = w; t.cx = t.x + t.wd / 2; t.cy = t.y + t.ht / 2; t.id = (uint16_t)pm_w16(w, 0x1F);
        o[n++] = t;
    }
    return n;
}

/* all connected pads together: D-pad, left stick (half way), A, B */
static int pm_read_pads(void)
{
    int i, m = 0;
    if (!g_pad_on || !g_pad_ready) return 0;
    EnterCriticalSection(&g_pad_cs);
    pad_refresh();
    for (i = 0; i < PAD_SLOTS; i++) {
        SDL_Gamepad *gp = g_pad[i].gp;
        int sx, sy;
        if (!gp) continue;
        if (psdl.GetGamepadButton(gp, 11)) m |= PM_UP;
        if (psdl.GetGamepadButton(gp, 12)) m |= PM_DOWN;
        if (psdl.GetGamepadButton(gp, 13)) m |= PM_LEFT;
        if (psdl.GetGamepadButton(gp, 14)) m |= PM_RIGHT;
        sx = psdl.GetGamepadAxis(gp, 0); sy = psdl.GetGamepadAxis(gp, 1);
        if (sy < -16000) m |= PM_UP;
        if (sy > 16000) m |= PM_DOWN;
        if (sx < -16000) m |= PM_LEFT;
        if (sx > 16000) m |= PM_RIGHT;
        if (psdl.GetGamepadButton(gp, 0)) m |= PM_A;
        if (psdl.GetGamepadButton(gp, 1)) m |= PM_B;
    }
    LeaveCriticalSection(&g_pad_cs);
    return m;
}

/* nearest object in a direction: distance along it (centre to centre) plus twice the
   sideways gap between the two rectangles (0 when they overlap, so a left-aligned list of
   words of different lengths is a straight line); only objects inside a cone round the
   direction (sideways gap at most twice the distance along) */
static int pm_step(const pm_obj *o, int n, int cur, int dir)
{
    int i, best = -1;
    long bs = 0;
    for (i = 0; i < n; i++) {
        long dx = o[i].cx - o[cur].cx, dy = o[i].cy - o[cur].cy, along, side, s;
        if (i == cur) continue;
        if (dir == PM_UP || dir == PM_DOWN) {
            along = dir == PM_UP ? -dy : dy;
            side = o[i].x > o[cur].x + o[cur].wd ? o[i].x - (o[cur].x + o[cur].wd)
                 : o[cur].x > o[i].x + o[i].wd ? o[cur].x - (o[i].x + o[i].wd) : 0;
        } else {
            along = dir == PM_LEFT ? -dx : dx;
            side = o[i].y > o[cur].y + o[cur].ht ? o[i].y - (o[cur].y + o[cur].ht)
                 : o[cur].y > o[i].y + o[i].ht ? o[cur].y - (o[i].y + o[i].ht) : 0;
        }
        if (along <= 0) continue;
        if (side > 2 * along + 8) continue;
        s = along + 2 * side;
        if (best < 0 || s < bs) { best = i; bs = s; }
    }
    return best;
}

static void pm_box(const pm_obj *t)
{
    if (t) {
        g_stats.nav_box[0] = t->x - 3; g_stats.nav_box[1] = t->y - 3;
        g_stats.nav_box[2] = t->wd + 6; g_stats.nav_box[3] = t->ht + 6;
    } else {
        g_stats.nav_box[2] = 0;
    }
    g_stats.nav_time = (long)GetTickCount();
}

static void pm_put_mouse(const pm_obj *t)
{
    int32_t off, x;
    memcpy(&off, (const void *)(uintptr_t)0x5F389C, 4);
    x = t->cx - (off >> 16);
    *(volatile int32_t *)(uintptr_t)0x62A30C = x;
    *(volatile int32_t *)(uintptr_t)0x62A308 = t->cy;
    g_pm_mouse_x = x;
    g_pm_mouse_y = t->cy;
}

static void pm_esc(int down)
{
    *(volatile uint8_t *)(uintptr_t)0x5D7015 = (uint8_t)(down != 0);
    *(volatile uint32_t *)(uintptr_t)0x62A7CC = down ? 1u : 0x81u;
}

static void pm_release_all(void)
{
    if (g_pm_clicking) { *(volatile int32_t *)(uintptr_t)0x62A304 &= ~1; g_pm_clicking = 0; }
    if (g_pm_escaping) { pm_esc(0); g_pm_escaping = 0; }
}

void pad_menu_tick(void) __asm__("nba98_pad_menu_tick");
void pad_menu_tick(void)
{
    static pm_obj o[PM_SLOTS];
    DWORD now = GetTickCount();
    int n, i, cur = -1, held, press, dir = 0;
    long steps = g_stats.sim_steps;

    if (steps != g_pm_steps) { g_pm_steps = steps; g_pm_steps_time = now; }
    if (!g_pm_on || now - g_pm_steps_time < 300) {     /* off, or a match is running */
        pm_release_all();
        g_pm_sel = NULL;
        if (g_stats.nav_box[2]) pm_box(NULL);
        g_pm_last_tick = now;
        return;
    }
    held = pm_read_pads();
    if (now - g_pm_last_tick > 500) g_pm_held = held;   /* first frame of a menu: buttons must be let go first */
    g_pm_last_tick = now;
    press = held & ~g_pm_held;
    g_pm_held = held;

    n = pm_collect(o);
#if NBA98_DEBUG
    {   /* Debug: list what a screen offers, once per screen */
        static const uint8_t *seen[PM_SLOTS];
        static int seen_n = -1;
        int same = n == seen_n;
        for (i = 0; i < n && same; i++) same = seen[i] == o[i].w;
        if (!same && (held || g_pm_sel)) {
            seen_n = n;
            for (i = 0; i < n; i++) seen[i] = o[i].w;
            for (i = 0; i < PM_SLOTS; i++) {
                const uint8_t *w = *(const uint8_t *const *)(uintptr_t)(0x5F9258 + 4 * i);
                if (pm_live(w))
                    LOG("menu pad: slot %d id 0x%x at %d,%d active %d grey %d/%d flags 0x%x centred %d", i,
                        (uint16_t)pm_w16(w, 0x1F), pm_w16(w, 0x1B), pm_w16(w, 0x1D), w[0x43], w[0x45], w[0x46], w[0x69], w[0x48]);
            }
        }
    }
#endif
    for (i = 0; i < n && g_pm_sel; i++) if (o[i].w == g_pm_sel) cur = i;
    if (cur < 0) g_pm_sel = NULL;                      /* the screen changed or the object went away */

    /* the real mouse moved: give it the menu back */
    if (g_pm_sel && !g_pm_clicking &&
        (*(volatile int32_t *)(uintptr_t)0x62A30C != g_pm_mouse_x || *(volatile int32_t *)(uintptr_t)0x62A308 != g_pm_mouse_y)) {
        g_pm_sel = NULL;
        cur = -1;
    }

    if (press & PM_DIRS) {
        dir = press & PM_DIRS;
        g_pm_dir_time = now;
        g_pm_dir_next = now + 400;
    } else if ((held & PM_DIRS) && (int)(now - g_pm_dir_next) >= 0) {
        dir = held & PM_DIRS;                          /* held: repeat */
        g_pm_dir_next = now + 150;
    }
    if (n && (dir || (press & PM_A)) && cur < 0) {
        /* first use on this screen: the object under the mouse, else the screen's first button
           (table order is creation order: top left), the arrows (ids 0x0C, 0x0D) last */
        int32_t off, mx = *(volatile int32_t *)(uintptr_t)0x62A30C, my = *(volatile int32_t *)(uintptr_t)0x62A308;
        memcpy(&off, (const void *)(uintptr_t)0x5F389C, 4);
        mx += off >> 16;
        for (i = 0; i < n && cur < 0; i++)
            if (mx >= o[i].x && mx < o[i].x + o[i].wd && my >= o[i].y && my < o[i].y + o[i].ht) cur = i;
        for (i = 0; i < n && cur < 0; i++) if (o[i].id != 0x0C && o[i].id != 0x0D) cur = i;
        if (cur < 0) cur = 0;
        g_pm_sel = o[cur].w;
        pm_put_mouse(&o[cur]);
        LOG("menu pad: selected id 0x%x at %d,%d %dx%d (%d objects)", o[cur].id, o[cur].x, o[cur].y, o[cur].wd, o[cur].ht, n);
        dir = 0;
        press &= ~PM_A;                                /* the first press only shows where we are */
    } else if (dir && cur >= 0) {
        int d, next = -1;
        for (d = PM_UP; d <= PM_RIGHT && next < 0; d <<= 1) if (dir & d) next = pm_step(o, n, cur, d);
        if (next >= 0) {
            cur = next;
            g_pm_sel = o[cur].w;
            pm_put_mouse(&o[cur]);
            LOG("menu pad: selected id 0x%x at %d,%d %dx%d", o[cur].id, o[cur].x, o[cur].y, o[cur].wd, o[cur].ht);
        }
    }

    /* A: left mouse button down while held, at the selected object */
    if ((press & PM_A) && cur >= 0) {
        pm_put_mouse(&o[cur]);
        *(volatile int32_t *)(uintptr_t)0x62A304 |= 1;
        g_pm_clicking = 1;                             /* counts the menu frames it has been down */
        LOG("menu pad: A on id 0x%x", o[cur].id);
    } else if (g_pm_clicking) {
        if (!(held & PM_A) && g_pm_clicking >= 3) {
            *(volatile int32_t *)(uintptr_t)0x62A304 &= ~1;
            g_pm_clicking = 0;
        } else {
            *(volatile int32_t *)(uintptr_t)0x62A304 |= 1;
            g_pm_clicking++;
        }
    }
    /* B: Esc while held */
    if (press & PM_B) {
        pm_esc(1);
        g_pm_escaping = 1;
        LOG("menu pad: B (Esc)");
    } else if (g_pm_escaping) {
        if (!(held & PM_B) && g_pm_escaping >= 3) {
            pm_esc(0);
            g_pm_escaping = 0;
        } else {
            pm_esc(1);
            g_pm_escaping++;
        }
    }

    pm_box(cur >= 0 ? &o[cur] : NULL);
}

__asm__(".text\n"
        /* replaces the first five bytes of the UI event poll 0x4585E4 (push ebx, ecx, edx, esi, edi) */
        "nba98_pad_menu_poll_stub:\n"
        "  pushal\n"
        "  pushfl\n"
        "  cld\n"
        "  call nba98_pad_menu_tick\n"
        "  popfl\n"
        "  popal\n"
        "  pushl %ebx\n"
        "  pushl %ecx\n"
        "  pushl %edx\n"
        "  pushl %esi\n"
        "  pushl %edi\n"
        "  pushl $0x4585E9\n"
        "  ret\n"
        /* replaces the first five bytes of the game's joystick menu code 0x48C454
           (push ebx, ecx, edx, edi, ebp) */
        "nba98_pad_menu_gamejoy_stub:\n"
        "  cmpl $0, nba98_pm_block_game_joy\n"
        "  je 1f\n"
        "  ret\n"
        "1:\n"
        "  pushl %ebx\n"
        "  pushl %ecx\n"
        "  pushl %edx\n"
        "  pushl %edi\n"
        "  pushl %ebp\n"
        "  pushl $0x48C459\n"
        "  ret\n");
void pad_menu_poll_stub(void) __asm__("nba98_pad_menu_poll_stub");
void pad_menu_gamejoy_stub(void) __asm__("nba98_pad_menu_gamejoy_stub");

static void pad_reload_menus(void)
{
    g_pm_on = (int)GetPrivateProfileIntA("input", "PadMenus", 1, g_ini) != 0;
    g_pm_block_game_joy = g_pm_on && g_pm_patched == 2;
    if (!g_pm_on) {
        g_pm_sel = NULL;
        g_stats.nav_box[2] = 0;
    }
    LOG("menu pad: %s", g_pm_on ? "on" : "off");
}

static void pad_menu_setup(void)
{
    static const unsigned char expect[5] = {0x53, 0x51, 0x52, 0x56, 0x57};   /* push ebx, ecx, edx, esi, edi */
    static const unsigned char expect_joy[5] = {0x53, 0x51, 0x52, 0x57, 0x55}; /* push ebx, ecx, edx, edi, ebp */
    unsigned char jmp[5];
    uint32_t rel;
    if (!g_is_v11) { LOG("menu pad: not the verified v1.1 exe; off"); return; }
    if (!g_pad_on) { LOG("menu pad: off (Gamepads=0)"); return; }
    jmp[0] = 0xE9;
    rel = (uint32_t)(uintptr_t)pad_menu_poll_stub - (0x4585E4 + 5);
    memcpy(jmp + 1, &rel, 4);
    if (!opt_patch(0x4585E4, jmp, 5, expect)) { LOG("menu pad: the exe code differs from what was expected; off"); return; }
    g_pm_patched = 1;
    rel = (uint32_t)(uintptr_t)pad_menu_gamejoy_stub - (0x48C454 + 5);
    memcpy(jmp + 1, &rel, 4);
    if (opt_patch(0x48C454, jmp, 5, expect_joy)) g_pm_patched = 2;
    else LOG("menu pad: the game's joystick menu code differs from what was expected; left on");
    pad_reload_menus();
}
