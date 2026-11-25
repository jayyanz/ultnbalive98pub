/*
 * glide2x.dll for NBA Live 98 - game window glue.
 *
 * The game believes its window is the 640x480 Glide screen: it reads the
 * mouse from WM_MOUSEMOVE / button messages in client pixels and moves the
 * cursor with ClientToScreen + SetCursorPos. Our window covers the monitor
 * with a scaled 4:3 image, so:
 *  - mouse messages are mapped from window pixels to 640x480 Glide pixels
 *    (window subclass), clamped to the screen;
 *  - the exe's ClientToScreen maps 640x480 Glide pixels back to the window;
 *  - the exe's GetClientRect reports 640x480 for the game window.
 * The exe's import table entries are restored when the window closes or the
 * DLL unloads (the game unloads and reloads glide2x.dll during detection).
 */
#include "nba98_cxx.h"   /* C++ build settings, first (0.7.15) */
#include "gl_internal.h"

NBA98_C_BEGIN
typedef BOOL (WINAPI *ClientToScreen_fn)(HWND, LPPOINT);
typedef BOOL (WINAPI *GetClientRect_fn)(HWND, LPRECT);

static HWND g_hwnd;
static WNDPROC g_orig_proc;
static ClientToScreen_fn o_ClientToScreen;
static GetClientRect_fn o_GetClientRect;
static void **g_slot_c2s, **g_slot_gcr;

static void **find_iat_slot(HMODULE mod, const char *dll, const char *fn)
{
    BYTE *base = (BYTE *)mod;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    IMAGE_DATA_DIRECTORY *dd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *imp;
    if (!dd->VirtualAddress) return NULL;
    for (imp = (IMAGE_IMPORT_DESCRIPTOR *)(base + dd->VirtualAddress); imp->Name; imp++) {
        IMAGE_THUNK_DATA *names, *addrs;
        if (lstrcmpiA((const char *)(base + imp->Name), dll) != 0) continue;
        names = (IMAGE_THUNK_DATA *)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        addrs = (IMAGE_THUNK_DATA *)(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; names++, addrs++) {
            IMAGE_IMPORT_BY_NAME *ibn;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            ibn = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)ibn->Name, fn) == 0) return (void **)&addrs->u1.Function;
        }
    }
    return NULL;
}

static void *swap_slot(void **slot, void *value)
{
    DWORD old;
    void *prev;
    if (!slot) return NULL;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old)) return NULL;
    prev = *slot;
    *slot = value;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return prev;
}

/* window pixels -> 640x480 Glide pixels */
static LPARAM map_mouse(LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    int px = G.pic_w > 0 ? G.pic_x : G.view_x, py = G.pic_w > 0 ? G.pic_y : G.view_y;
    int pw = G.pic_w > 0 ? G.pic_w : G.view_w, ph = G.pic_w > 0 ? G.pic_h : G.view_h;
    if (pw > 0 && ph > 0) {
        x = (int)(((long long)(x - px) * FB_W) / pw);
        y = (int)(((long long)(y - py) * FB_H) / ph);
    }
    if (x < 0) x = 0;
    if (x > FB_W - 1) x = FB_W - 1;
    if (y < 0) y = 0;
    if (y > FB_H - 1) y = FB_H - 1;
    return MAKELPARAM(x, y);
}

/* The game sizes its own window for a 640x480 screen (seen on Windows: the
 * window shrank to 640x480 in the top-left corner after we had made it cover
 * the monitor). Keep the size we chose: the whole monitor when full screen,
 * our window size when windowed. */
static void keep_our_size(HWND h, WINDOWPOS *p)
{
    MONITORINFO mi;
    if (!p || (p->flags & SWP_NOSIZE && p->flags & SWP_NOMOVE)) return;
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi)) return;
    if (!G.cfg_windowed) {
        p->x = mi.rcMonitor.left;
        p->y = mi.rcMonitor.top;
        p->cx = mi.rcMonitor.right - mi.rcMonitor.left;
        p->cy = mi.rcMonitor.bottom - mi.rcMonitor.top;
        p->flags &= ~(UINT)SWP_NOMOVE;
    } else {
        RECT r;
        SetRect(&r, 0, 0, G.cfg_width > 0 ? G.cfg_width : 1280, G.cfg_height > 0 ? G.cfg_height : 960);
        AdjustWindowRect(&r, (DWORD)GetWindowLongA(h, GWL_STYLE), FALSE);
        p->cx = r.right - r.left;
        p->cy = r.bottom - r.top;
        if (!(p->flags & SWP_NOMOVE) && (p->x < mi.rcWork.left || p->y < mi.rcWork.top)) {
            p->x = mi.rcWork.left + 40;
            p->y = mi.rcWork.top + 40;
        }
    }
    p->flags &= ~(UINT)(SWP_NOSIZE);
}

static LRESULT CALLBACK game_wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_WINDOWPOSCHANGING) keep_our_size(h, (WINDOWPOS *)lp);
    /* Ctrl+F11: show or hide the overlay (the game never sees this key) */
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_F11 && GetKeyState(VK_CONTROL) < 0) {
        if (!(lp & 0x40000000)) InterlockedIncrement(&G.overlay_toggles);   /* ignore auto-repeat */
        return 0;
    }
    if (NBA98_DEBUG && (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wp == VK_F10 && GetKeyState(VK_CONTROL) < 0 &&
        !(lp & 0x40000000)) {
        InterlockedIncrement(&G.trace_req);   /* Debug: trace the next frame's draws (logs\glide_trace_N.txt) */
        return 0;
    }
    /* Page Down: screenshot (0.7.12; the game never sees this key) */
    if (G.cfg_shots && (msg == WM_KEYDOWN || msg == WM_KEYUP) && wp == VK_NEXT) {
        if (msg == WM_KEYDOWN && !(lp & 0x40000000)) InterlockedIncrement(&G.shot_req);
        return 0;
    }
    if (msg >= WM_MOUSEMOVE && msg <= WM_MBUTTONDBLCLK) lp = map_mouse(lp);
    return CallWindowProcA(g_orig_proc, h, msg, wp, lp);
}

static BOOL WINAPI h_ClientToScreen(HWND h, LPPOINT p)
{
    if (h == g_hwnd && p && G.view_w > 0 && G.view_h > 0) {
        int px = G.pic_w > 0 ? G.pic_x : G.view_x, py = G.pic_w > 0 ? G.pic_y : G.view_y;
        int pw = G.pic_w > 0 ? G.pic_w : G.view_w, ph = G.pic_w > 0 ? G.pic_h : G.view_h;
        p->x = px + (LONG)(((long long)p->x * pw) / FB_W);
        p->y = py + (LONG)(((long long)p->y * ph) / FB_H);
    }
    return o_ClientToScreen(h, p);
}

static BOOL WINAPI h_GetClientRect(HWND h, LPRECT r)
{
    if (h == g_hwnd && r) { SetRect(r, 0, 0, FB_W, FB_H); return TRUE; }
    return o_GetClientRect(h, r);
}

void win_attach(HWND hwnd)
{
    HMODULE exe = GetModuleHandleA(NULL);
    if (g_hwnd) win_detach();
    g_hwnd = hwnd;
    g_orig_proc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)game_wndproc);
    if (!o_ClientToScreen) {
        g_slot_c2s = find_iat_slot(exe, "USER32.dll", "ClientToScreen");
        o_ClientToScreen = (ClientToScreen_fn)swap_slot(g_slot_c2s, (void *)h_ClientToScreen);
        if (!o_ClientToScreen) { o_ClientToScreen = ClientToScreen; g_slot_c2s = NULL; }
    }
    if (!o_GetClientRect) {
        g_slot_gcr = find_iat_slot(exe, "USER32.dll", "GetClientRect");
        o_GetClientRect = (GetClientRect_fn)swap_slot(g_slot_gcr, (void *)h_GetClientRect);
        if (!o_GetClientRect) { o_GetClientRect = GetClientRect; g_slot_gcr = NULL; }
    }
    glog("window: mouse mapped to the 640x480 Glide screen (subclass %s, ClientToScreen %s, GetClientRect %s)",
         g_orig_proc ? "ok" : "FAILED", g_slot_c2s ? "hooked" : "not hooked", g_slot_gcr ? "hooked" : "not hooked");
}

void win_detach(void)
{
    if (g_hwnd && g_orig_proc && IsWindow(g_hwnd) &&
        (WNDPROC)GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC) == game_wndproc)
        SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_orig_proc);
    if (g_slot_c2s && o_ClientToScreen) swap_slot(g_slot_c2s, (void *)o_ClientToScreen);
    if (g_slot_gcr && o_GetClientRect) swap_slot(g_slot_gcr, (void *)o_GetClientRect);
    g_slot_c2s = g_slot_gcr = NULL;
    o_ClientToScreen = NULL;
    o_GetClientRect = NULL;
    g_hwnd = NULL;
    g_orig_proc = NULL;
}
NBA98_C_END
