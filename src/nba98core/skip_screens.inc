/*
 * Skip the start-up and exit screens (0.7.12; included by nba98core.cpp after optmenu.inc).
 *
 * The front end is a screen state machine: the state byte 0x5C8284 picks a handler from the
 * table at 0x4C3E38 (the previous state goes to 0x5C8285 and a history list at 0x61CEAC).
 *   state 1  0x4C4197: EA Sports movie (easports.dct, play 0x50A81C), then the legal screen
 *            (1legal.fsh, 0x4ED180 / 0x4ED1F8 / 0x4ED1E4), which picks the next state (2)
 *   state 2  0x4C420B: the intro movie (intro.dct), then state 3
 *   state 3  0x4C423C: the NBA Live 98 title screen (1title0.fsh), then state 4 (Game Setup)
 *   state 5  0x4C42D4: credits (0x51AD50), then state 0x48: special thanks (0x51F8BC), then 6
 *   state 6  0x4C43D0: the exit (saves, restores the desktop)
 *
 * [nba98] SkipIntros=1 (default): state 1's "mov eax, offset easports.dct" at 0x4C41B3 jumps to
 * our code, which clears 0x5C6650 as the movie path does and goes on to state 3 the way every
 * handler ends (so the EA Sports movie, the legal screen and the intro movie are skipped; the
 * title screen and the main menu come up as usual). Applies at the next start.
 * [nba98] SkipCredits=1 (default): Game Setup's quit (0x49DDF0: 0x4DA6A8(0, 3), then
 * "mov byte [esp+0x128], 5" at 0x49DDFC) asks for state 6 instead of 5, as our restart does, so
 * the credits and special thanks are skipped. Applies at once (Enhanced Options, User Interface).
 */

__asm__(".text\n"
        "nba98_skip_intro_stub:\n"
        "  cmpl $0, nba98_skip_intros\n"
        "  jne 1f\n"
        "  movl $0x5ABD44, %eax\n"            /* as the original: easports.dct, then on with the movie */
        "  pushl $0x4C41B8\n"
        "  ret\n"
        "1:\n"
        "  movb $0, 0x5C6650\n"
        "  movb 0x5C8284, %al\n"
        "  movl 0x5C828A, %edx\n"
        "  movb %al, 0x5C8285\n"
        "  sarl $16, %edx\n"
        "  movb $3, 0x5C8284\n"               /* the title screen next */
        "  pushl $0x4C412A\n"
        "  ret\n");
int g_skip_intros_asm __asm__("nba98_skip_intros");
void skip_intro_stub(void) __asm__("nba98_skip_intro_stub");

static void skip_credits_apply(void)
{
    static int patched = -1;
    unsigned char want = ini_int("SkipCredits", 1) ? 6 : 5, now;
    if (!g_is_v11) return;
    now = *(volatile unsigned char *)(uintptr_t)0x49DE03;
    if (now != 5 && now != 6) { if (patched < 0) LOG("skip exit credits: the exe code differs from what was expected; off"); patched = 0; return; }
    if (now != want) {
        unsigned char was = now;
        if (!opt_patch(0x49DE03, &want, 1, &was)) { LOG("skip exit credits: patch failed"); return; }
    }
    if (patched != (want == 6)) LOG("skip exit credits: %s", want == 6 ? "on (quit goes straight to the exit)" : "off (credits and special thanks on quit)");
    patched = want == 6;
}

static void skip_screens_setup(void)
{
    static const unsigned char e_quit[8] = {0xC6, 0x84, 0x24, 0x28, 0x01, 0x00, 0x00, 0x05};
    static const unsigned char e_intro[5] = {0xB8, 0x44, 0xBD, 0x5A, 0x00};
    unsigned char jmp[5];
    uint32_t rel;
    if (!g_is_v11) { LOG("skip screens: not the verified v1.1 exe; off"); return; }
    g_skip_intros_asm = ini_int("SkipIntros", 1) != 0;
    jmp[0] = 0xE9;
    rel = (uint32_t)(uintptr_t)skip_intro_stub - (0x4C41B3 + 5);
    memcpy(jmp + 1, &rel, 4);
    if (opt_patch(0x4C41B3, jmp, 5, e_intro))
        LOG("skip intros: %s", g_skip_intros_asm ? "on (EA Sports movie, legal screen and intro movie skipped; title screen, then Game Setup)" : "off");
    else
        LOG("skip intros: the exe code differs from what was expected; off");
    if (!memcmp((const void *)(uintptr_t)0x49DDFC, e_quit, 7)) skip_credits_apply();
    else LOG("skip exit credits: the exe code differs from what was expected; off");
}
