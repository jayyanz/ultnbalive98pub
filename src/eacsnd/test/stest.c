/*
 * Stand-alone test for eacsnd.dll: plays the part of NBAWIN.EXE.
 * Loads eacsnd.dll, hands it a Watcom-convention mixer (440 Hz sine, stereo
 * 16-bit) and tick callback, starts 22050 Hz stereo 16-bit output, calls
 * serve() every 5 ms for 3 s, then checks the tick rate and that every
 * register was preserved across each call.
 * Run with SDL_AUDIO_DRIVER=disk to capture the output to a file.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

static volatile long g_mixed, g_ticks, g_mix_calls;
static double g_phase;

void c_mix(int16_t *dst, int frames) __asm__("c_mix");
void c_tick(void) __asm__("c_tick");

void c_mix(int16_t *dst, int frames)
{
    int i;
    for (i = 0; i < frames; i++) {
        int16_t v = (int16_t)(sin(g_phase) * 8000.0);
        g_phase += 2.0 * 3.14159265358979 * 440.0 / 22050.0;
        dst[2 * i] = v;
        dst[2 * i + 1] = v;
    }
    g_mixed += frames;
    g_mix_calls++;
}
void c_tick(void) { g_ticks++; }

/* Watcom-convention callbacks: EAX = dest, EDX = frames; keep all but EAX */
void w_mix(void) __asm__("w_mix");
void w_tick(void) __asm__("w_tick");
__asm__(
    ".text\n"
    "w_mix:\n"
    "  pushl %ebx; pushl %ecx; pushl %edx; pushl %esi; pushl %edi; pushl %ebp\n"
    "  pushl %edx; pushl %eax; call c_mix; addl $8, %esp\n"
    "  popl %ebp; popl %edi; popl %esi; popl %edx; popl %ecx; popl %ebx\n"
    "  ret\n"
    "w_tick:\n"
    "  pushl %ebx; pushl %ecx; pushl %edx; pushl %esi; pushl %edi; pushl %ebp\n"
    "  call c_tick\n"
    "  popl %ebp; popl %edi; popl %esi; popl %edx; popl %ecx; popl %ebx\n"
    "  ret\n");

/* call a Watcom function with up to 4 register args (+1 stack arg) and check
   that EBX, ECX, EDX, ESI, EDI, EBP come back unchanged */
uint32_t W[16] __asm__("W");   /* fn, eax, edx, ebx, ecx, has_stack, stack, ret, ebx, ecx, edx, esi, edi, ebp, saved esp */
void wcall_asm(void) __asm__("wcall_asm");
__asm__(
    ".text\n"
    "wcall_asm:\n"
    "  pushl %ebx; pushl %esi; pushl %edi; pushl %ebp\n"
    "  movl %esp, W+56\n"
    "  movl $0x5a5a0001, %esi; movl $0x5a5a0002, %edi; movl $0x5a5a0003, %ebp\n"
    "  cmpl $0, W+20\n"
    "  je 1f\n"
    "  pushl W+24\n"
    "1:\n"
    "  movl W+4, %eax; movl W+8, %edx; movl W+12, %ebx; movl W+16, %ecx\n"
    "  call *W\n"
    "  movl %eax, W+28; movl %ebx, W+32; movl %ecx, W+36; movl %edx, W+40\n"
    "  movl %esi, W+44; movl %edi, W+48; movl %ebp, W+52\n"
    "  cmpl W+56, %esp\n"
    "  je 2f\n"
    "  movl $0xdead, W+52\n"   /* stack not balanced */
    "  movl W+56, %esp\n"
    "2:\n"
    "  popl %ebp; popl %edi; popl %esi; popl %ebx\n"
    "  ret\n");

static int g_reg_errors;
static uint32_t wcall(void *fn, uint32_t a, uint32_t d, uint32_t b, uint32_t c, int stack_arg, uint32_t s)
{
    W[0] = (uint32_t)(uintptr_t)fn; W[1] = a; W[2] = d; W[3] = b; W[4] = c; W[5] = stack_arg; W[6] = s;
    wcall_asm();
    if (W[8] != b || W[9] != c || W[10] != d || W[11] != 0x5a5a0001 || W[12] != 0x5a5a0002 || W[13] != 0x5a5a0003) {
        g_reg_errors++;
        printf("registers/stack not preserved: ebx %08x ecx %08x edx %08x esi %08x edi %08x ebp %08x\n",
               W[8], W[9], W[10], W[11], W[12], W[13]);
    }
    return W[7];
}

int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA(argc > 1 ? argv[1] : "eacsnd.dll");
    void *ver, *setf, *caps, *start, *stop, *serve;
    uint32_t r;
    int i;
    DWORD t0;
    if (!m) { printf("cannot load eacsnd.dll\n"); return 1; }
    ver = GetProcAddress(m, "iSNDdllversion_");
    setf = GetProcAddress(m, "iSNDdirectsetfunctions_");
    caps = GetProcAddress(m, "iSNDdirectcaps_");
    start = GetProcAddress(m, "iSNDdirectstart_");
    stop = GetProcAddress(m, "iSNDdirectstop_");
    serve = GetProcAddress(m, "iSNDdirectserve_");
    if (!ver || !setf || !caps || !start || !stop || !serve) { printf("missing export\n"); return 1; }
    r = wcall(ver, 0, 0x11, 0x22, 0x33, 0, 0);
    printf("dllversion 0x%x\n", r);
    r = wcall(setf, (uint32_t)(uintptr_t)w_mix, 0, 0x22, (uint32_t)(uintptr_t)w_tick, 1, 0x551ddd);
    printf("setfunctions %d\n", (int)r);
    r = wcall(caps, 0x1234, 0x11, 0x22, 0x33, 0, 0);
    printf("caps 0x%x\n", r);
    r = wcall(start, 0x0808, 0x1234, 0x22, 0x33, 0, 0);
    printf("start %d\n", (int)r);
    t0 = GetTickCount();
    for (i = 0; GetTickCount() - t0 < 3000; i++) {
        wcall(serve, 0x10, 0x11, 0x22, 0x33, 0, 0);
        Sleep(5);
    }
    r = wcall(stop, 0x10, 0x11, 0x22, 0x33, 0, 0);
    printf("stop %d; %d serves, %ld frames mixed in %ld calls, %ld ticks (%.1f per s of audio), %d register errors\n",
           (int)r, i, g_mixed, g_mix_calls, g_ticks, g_mixed ? g_ticks * 22050.0 / g_mixed : 0.0, g_reg_errors);
    return g_reg_errors ? 2 : 0;
}
