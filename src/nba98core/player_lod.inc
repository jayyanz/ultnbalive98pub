/*
 * Distance detail for players (0.7.8; findings in the player LOD and distance detail docs).
 *
 * The game picks each player's uniform texture and body model from his depth in camera space
 * (Z, the float at 0x60DED8 + 36 x player), with fixed limits made for a 640x480 screen:
 *   uniform (selector 0x4B5E50): 256x256 below 850, 128 to 1700, 64 to 3400, 32 beyond
 *     (floats at 0x5AABAC, 0x5AABB0, 0x5AABB4, one reference each);
 *   model, reflections, shadows (0x4B8DA8): limits 700, 1400, 2800 as the immediates of three
 *     "cmp edx, imm32" (float bits) at 0x4B8DB9, 0x4B8DC1, 0x4B8DC9.
 * In live play every player is at 1,000 to 4,000, so nobody gets the 256 sheet, and at 4K the
 * 64 and 32 sheets are stretched far past what they were made for.
 *
 * [nba98] DistanceDetail scales all six limits:
 *   original  x1 (as the game shipped);
 *   auto      x (picture height / 480), from the height glide2x draws matches at (stats
 *             render_h, the height the 3D is drawn at, CRT filter or not since 0.7.9); about x4.5 at 2160, which
 *             puts every live-play player on the top tier;
 *   full      every player always on the top tier.
 * Applied on the game thread: at start, at every match viewport setup (the widescreen hook at
 * 0x42C7E0, so Auto follows the window) and when the options row is changed. Rendering only:
 * the limits feed nothing but the texture and model choice.
 *
 * The 256 sheets come from a 10-slot cache filled at match load (slot owner 0x60D920 + 12 x
 * slot, -1 = refused for lack of texture memory). The timing log shows each second, in a
 * match, how many players are on each texture tier and how many slots were granted.
 */

static const float k_lod_tex[3] = {850.0f, 1700.0f, 3400.0f};
static const float k_lod_mdl[3] = {700.0f, 1400.0f, 2800.0f};
static const uint32_t k_lod_tex_va[3] = {0x5AABAC, 0x5AABB0, 0x5AABB4};
static const uint32_t k_lod_mdl_va[3] = {0x4B8DBB, 0x4B8DC3, 0x4B8DCB};   /* imm32 of cmp edx at 0x4B8DB9.. */
#define LOD_FULL 1.0e7f            /* deeper than anything is ever drawn (far plane 20000) */

static int g_lod_ok;               /* patch points verified */
static int g_lod_mode = -1;        /* 0 original, 1 auto, 2 full; -1 = read from the ini */
static float g_lod_scale = -1.0f;  /* last applied (-1 = none yet, so the first apply is logged) */
static long g_lod_slots_logged = -2;

static int lod_mode_ini(void)
{
    char v[16];
    GetPrivateProfileStringA("nba98", "DistanceDetail", "auto", v, sizeof v, g_ini);
    if (!_stricmp(v, "original") || !strcmp(v, "0")) return 0;
    if (!_stricmp(v, "full") || !strcmp(v, "2")) return 2;
    return 1;
}

/* the factor the current setting asks for */
static float lod_scale_wanted(void)
{
    long h = g_stats.render_h;
    if (g_lod_mode == 2) return 0;   /* 0 = full */
    if (g_lod_mode == 1 && h > 480) return (float)h / 480.0f;
    return 1.0f;
}

static void lod_apply(void)
{
    float s, v;
    int i;
    if (!g_lod_ok) return;
    if (g_lod_mode < 0) g_lod_mode = lod_mode_ini();
    s = lod_scale_wanted();
    if (s == g_lod_scale) return;
    for (i = 0; i < 3; i++) {
        v = s ? k_lod_tex[i] * s : LOD_FULL;
        opt_patch(k_lod_tex_va[i], &v, 4, NULL);
        v = s ? k_lod_mdl[i] * s : LOD_FULL;
        opt_patch(k_lod_mdl_va[i], &v, 4, NULL);
    }
    g_lod_scale = s;
    if (s) LOG("distance detail: %s, limits x%.2f (uniform 256 below %.0f, 128 below %.0f, 64 below %.0f; full model below %.0f)",
               g_lod_mode == 1 ? "auto" : "original", s, k_lod_tex[0] * s, k_lod_tex[1] * s, k_lod_tex[2] * s, k_lod_mdl[1] * s);
    else LOG("distance detail: full (every player on the top tier)");
}

/* the options row: next value, saved and applied at once */
static void lod_set_mode(int m)
{
    static const char *const k_ini[3] = {"original", "auto", "full"};
    g_lod_mode = m;
    opt_ini_set("nba98", "DistanceDetail", k_ini[m]);
    lod_apply();
}

static int lod_mode(void)
{
    if (g_lod_mode < 0) g_lod_mode = lod_mode_ini();
    return g_lod_mode;
}

/* timing log (once a second, in a match): players on each uniform tier, 256 slots granted */
static void lod_report(char *out, size_t cap)
{
    int i, n[4] = {0, 0, 0, 0}, slots = 0;
    float lim[3];
    out[0] = 0;
    if (!g_lod_ok) return;
    for (i = 0; i < 3; i++) memcpy(&lim[i], (const void *)(uintptr_t)k_lod_tex_va[i], 4);
    for (i = 0; i < 10; i++) {
        float z = *(volatile float *)(uintptr_t)(0x60DED8 + 36 * i);
        n[z < lim[0] ? 0 : z < lim[1] ? 1 : z < lim[2] ? 2 : 3]++;
        if (*(volatile long *)(uintptr_t)(0x60D920 + 12 * i) >= 0) slots++;
    }
    _snprintf(out, cap, "  |  uniforms 256:%d 128:%d 64:%d 32:%d, 256 slots %d/10", n[0], n[1], n[2], n[3], slots);
    out[cap - 1] = 0;
    if (slots != g_lod_slots_logged) {
        g_lod_slots_logged = slots;
        LOG("distance detail: %d of 10 full-detail uniform slots granted%s", slots,
            slots < 10 ? " (short of texture memory: near players share the rest)" : "");
    }
}

static void player_lod_setup(void)
{
    static const unsigned char e_cmp[3][2] = {{0x81, 0xFA}, {0x81, 0xFA}, {0x81, 0xFA}};
    int i;
    if (!g_is_v11) { LOG("distance detail: not the verified v1.1 exe; original limits"); return; }
    for (i = 0; i < 3; i++) {
        float t, m;
        memcpy(&t, (const void *)(uintptr_t)k_lod_tex_va[i], 4);
        memcpy(&m, (const void *)(uintptr_t)k_lod_mdl_va[i], 4);
        if (t != k_lod_tex[i] || m != k_lod_mdl[i] || memcmp((const void *)(uintptr_t)(k_lod_mdl_va[i] - 2), e_cmp[i], 2)) {
            LOG("distance detail: the exe differs from what was expected; original limits");
            return;
        }
    }
    g_lod_ok = 1;
    lod_apply();
}
