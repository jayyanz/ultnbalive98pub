/*
 * Widescreen matches, Hor+ (0.7.5, roadmap item 1; plan in the phase 2 widescreen doc).
 *
 * The match camera is set up by 0x42C7E0 (viewport and projection). Its horizontal
 * half-angle of view is the float at 0x5A1098 (1/15 turn = 24 degrees, 48 degree view)
 * scaled by the viewport width; the vertical one is 0x5A109C (18 degrees). Culling,
 * clipping and projection all follow from these, so a wider view is one constant: with
 * tan(new) = tan(24 degrees) x aspect / (4/3) the vertical view is unchanged and the sides
 * show more court. The game keeps drawing into its 640x480 space, squeezed; glide2x
 * un-squeezes the 3D onto the full width of the screen.
 *
 * glide2x tells us the aspect it draws matches at (stats ws_aspect, 0 = off). We set the
 * constant at the start of every viewport setup (jump at 0x42C7E0, whose first six bytes are
 * push esi, push edi, push ebp, sub esp 0x28), so it follows the setting and window.
 *
 * The match frame (0x42CF14 -> 0x42CAD0) renders the 3D world in 0x42D118 (it sets up the
 * viewport and draws the arena, court and players) and draws the overlays after it returns
 * (0x42BA74 and 0x4651C0, or 0x46787C, then the cursor 0x48B7A4). The world call is at
 * 0x42CB2C; a second frame routine (0x426461) has the same call at 0x42648C. Our thunk on both
 * calls runs the world render and then sets stats draw_2d; glide2x draws everything from then
 * until its next buffer swap in the centred 4:3 area, at the HUD's own proportions.
 * (The first try, 0.7.5 Wine test, marked 0x42CAD0 itself: that is too early, since it
 * includes the world render, and the whole match came out in the 4:3 area.)
 *
 * Rendering only: no game state is written, apart from the camera constant, which feeds
 * nothing but the projection (to confirm in a net test before relying on it online).
 */

static const float k_ws_fov_orig = 1.0f / 15.0f;

uint32_t g_ws_vp_caller __asm__("nba98_ws_vp_caller");
static long g_ws_world_n, g_ws_vp_n;

/* Debug probe (0.7.11 investigation): what projection the first match frames use */
void ws_world_pre(void) __asm__("nba98_ws_world_pre");
void ws_world_pre(void)
{
    static float lx = -1, ly = -1;
    volatile float *pr = (volatile float *)(uintptr_t)0x5D9CF8;
    g_ws_world_n++;
    g_stats.match_3d = 1;   /* glide2x: the 3D that follows is the match, even before the first buffer swap */
    g_stats.match_state = *(volatile unsigned char *)(uintptr_t)0x5C8284;   /* glide2x: pause screens in this state are the match too */
    if (NBA98_DEBUG && (pr[0] != lx || pr[1] != ly)) {
        LOG("ws probe: world render %ld: projection scale %.2f %.2f centre %.2f %.2f (viewport setups so far %ld)",
            g_ws_world_n, pr[0], pr[1], pr[2], pr[3], g_ws_vp_n);
        lx = pr[0]; ly = pr[1];
    }
}

void ws_viewport_hook(void) __asm__("nba98_ws_viewport_hook");
void ws_viewport_hook(void)
{
    float a = g_stats.ws_aspect, k = k_ws_fov_orig;
    static float last = -1;
    lod_apply();   /* distance detail follows the picture height (player_lod.inc) */
    g_ws_vp_n++;
    if (g_ws_vp_caller != 0x48D100) g_stats.match_3d = 1;   /* not the call at 0x48D0FB (outside the match frame) */
    if (NBA98_DEBUG && g_ws_vp_n <= 20) LOG("ws probe: viewport setup %ld from 0x%08x (world renders so far %ld)", g_ws_vp_n, g_ws_vp_caller, g_ws_world_n);
    if (a > 1.34f && a < 4.0f)
        k = (float)(atan(tan(24.0 * 3.14159265358979 / 180.0) * a / (4.0 / 3.0)) / (2.0 * 3.14159265358979));
    *(volatile float *)(uintptr_t)0x5A1098 = k;
    g_stats.ws_fov = k == k_ws_fov_orig ? 0.0f : k;
    if (k != last) {
        LOG("widescreen: match camera %s (aspect %.3f, half-angle %.6f turn, view %.1f degrees)",
            k == k_ws_fov_orig ? "original 4:3" : "wider", a, k, 720.0 * k);
        last = k;
    }
}

__asm__(".text\n"
        /* replaces the first six bytes of the match viewport setup 0x42C7E0 */
        "nba98_ws_viewport_stub:\n"
        "  pushal\n"
        "  pushfl\n"
        "  cld\n"
        "  movl 36(%esp), %eax\n"
        "  movl %eax, nba98_ws_vp_caller\n"
        "  call nba98_ws_viewport_hook\n"
        "  popfl\n"
        "  popal\n"
        "  pushl %esi\n"
        "  pushl %edi\n"
        "  pushl %ebp\n"
        "  subl $0x28, %esp\n"
        "  pushl $0x42C7E6\n"
        "  ret\n"
        /* replaces "call 0x42D118" (the match world render) at 0x42CB2C and 0x42648C:
           the world, then the HUD marker (registers as the render left them) */
        "nba98_ws_world_stub:\n"
        "  pushal\n"
        "  pushfl\n"
        "  cld\n"
        "  call nba98_ws_world_pre\n"
        "  popfl\n"
        "  popal\n"
        "  call *nba98_ws_world_fn\n"
        "  pushl %eax\n"
        "  movl nba98_ws_draw2d, %eax\n"
        "  movl $1, (%eax)\n"
        "  popl %eax\n"
        "  ret\n");
uint32_t g_ws_world_fn __asm__("nba98_ws_world_fn") = 0x42D118;
volatile long *g_ws_draw2d __asm__("nba98_ws_draw2d") = &g_stats.draw_2d;
void ws_viewport_stub(void) __asm__("nba98_ws_viewport_stub");
void ws_world_stub(void) __asm__("nba98_ws_world_stub");

static void widescreen_setup(void)
{
    static const unsigned char e_vp[6] = {0x56, 0x57, 0x55, 0x83, 0xEC, 0x28};
    static const unsigned char e_w1[5] = {0xE8, 0xE7, 0x05, 0x00, 0x00};   /* call 0x42D118 at 0x42CB2C */
    static const unsigned char e_w2[5] = {0xE8, 0x87, 0x6C, 0x00, 0x00};   /* call 0x42D118 at 0x42648C */
    unsigned char jmp[6], call[5];
    uint32_t rel;
    float k;
    memcpy(&k, (const void *)(uintptr_t)0x5A1098, 4);
    if (!g_is_v11 || k != k_ws_fov_orig) { LOG("widescreen: not the verified v1.1 exe; matches stay 4:3"); return; }
    jmp[0] = 0xE9;
    rel = (uint32_t)(uintptr_t)ws_viewport_stub - (0x42C7E0 + 5);
    memcpy(jmp + 1, &rel, 4);
    jmp[5] = 0x90;
    call[0] = 0xE8;
    rel = (uint32_t)(uintptr_t)ws_world_stub - (0x42CB2C + 5);
    memcpy(call + 1, &rel, 4);
    if (!opt_patch(0x42CB2C, call, 5, e_w1)) { LOG("widescreen: the exe code differs from what was expected; matches stay 4:3"); return; }
    rel = (uint32_t)(uintptr_t)ws_world_stub - (0x42648C + 5);
    memcpy(call + 1, &rel, 4);
    if (!opt_patch(0x42648C, call, 5, e_w2) || !opt_patch(0x42C7E0, jmp, 6, e_vp)) {
        LOG("widescreen: the exe code differs from what was expected; matches stay 4:3 (HUD marker may be in)");
        return;
    }
    LOG("widescreen: match camera and HUD marker in (glide2x decides the aspect)");
}
