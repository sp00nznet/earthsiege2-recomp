/*
 * Earth Siege 2 - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32. The game is two executables
 * that ES.EXE ran in turn: VSHELL.EXE (the shell: menus, hangar, briefings)
 * and DBSIM.EXE (the sim: piloting a HERC). Each is lifted whole and built
 * into its own host from this file (ES2_EXE names the one): es2shell.exe and
 * es2sim.exe. Their Borland runtime (cw3220.dll, bwcc32.dll) and the sound
 * library (sos9503.dll) load natively.
 *
 * --play is ES.EXE: a loop that runs the shell or the sim as a child process
 * and takes the child's exit code as the next state (docs/bringup.md).
 *
 * The game reads its own switches from the command line (cw3220 asks
 * Windows for it), so every argument with a single dash is the game's: the
 * host leaves those alone, and the game ignores the host's "--" ones.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"

#define EXE_BASE 0x00400000u
#define ARG(n) MEM32(g_esp + 4 + 4 * (n))

static native32_module_t g_mods[] = { { ES2_EXE, EXE_BASE } };
static char g_game_dir[MAX_PATH];
static DWORD g_watchdog_s;
static void record_close(void);

static void shim_MessageBoxA(void) {
    fprintf(stderr, "[messagebox] %s: %s\n", (char*)(uintptr_t)ARG(2), (char*)(uintptr_t)ARG(1));
    g_eax = IDOK;
    g_esp += 4 + 4 * 4;
}

static void shim_MessageBoxW(void) {
    fprintf(stderr, "[messagebox] %ls: %ls\n", (wchar_t*)(uintptr_t)ARG(2), (wchar_t*)(uintptr_t)ARG(1));
    recomp_dump_trace("messagebox");
    g_eax = IDOK;
    g_esp += 4 + 4 * 4;
}

static native32_shim_t g_shims[] = {
    { "MessageBoxA", shim_MessageBoxA },
    { "MessageBoxW", shim_MessageBoxW },
};

/* ---- diagnostics ---------------------------------------------------------- */

void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "\n[not-lifted] sub_%08X  (called from 0x%08X)\n", va, g_cur_func);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 0x102);   /* not 1..6: those are ES states */
}

recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return NULL; }

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    HMODULE hm;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    /* A fault inside a Windows DLL is its own business first (ddraw probes
     * with __try); only the host (lifted code) and code outside any module. */
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)r->ExceptionAddress, &hm) && hm != GetModuleHandleA(NULL))
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&once, 1)) { Sleep(5000); TerminateProcess(GetCurrentProcess(), 0x103); }
    fprintf(stderr, "\n=== fault 0x%08lX at %p, thread %lu ===\n", r->ExceptionCode, r->ExceptionAddress,
            GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
        fprintf(stderr, "  %s of 0x%08X\n", r->ExceptionInformation[0] == 0 ? "read" :
                r->ExceptionInformation[0] == 1 ? "write" : "execute", (uint32_t)r->ExceptionInformation[1]);
    fprintf(stderr, "  in lifted sub_%08X, last native call %s\n", g_cur_func, g_cur_import);
    fprintf(stderr, "  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
            g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    native32_dump_icalls(12);
    recomp_trace_flush();
    TerminateProcess(GetCurrentProcess(), 0x103);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X, last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    recomp_trace_flush();
    fflush(stderr);
    record_close();
    TerminateProcess(GetCurrentProcess(), 0x104);
    return 0;
}

/* ---- --shot S FILE and --record FILE.mp4: the frames the game presents ---
 * Taken from the surface the game blits to its window (native32_on_present):
 * on the private desktop of --headless a GDI copy of the window is black. */
static DWORD g_shot_s[16];
static char g_shot_file[16][MAX_PATH];
static int g_shots;
static volatile LONG g_shot_due = -1;
static char g_record[MAX_PATH];
static FILE* g_rec;
static int g_rec_w, g_rec_h, g_rec_bits;

static BOOL CALLBACK find_window(HWND w, LPARAM out) {
    DWORD pid;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w)) { *(HWND*)out = w; return FALSE; }
    return TRUE;
}

static HWND game_window(void) {
    HWND w = NULL;
    EnumWindows(find_window, (LPARAM)&w);
    return w;
}

static void write_bmp(const char* file, int w, int h, const uint8_t* bgr, DWORD stride) {
    BITMAPINFOHEADER bh = { sizeof bh, w, h, 1, 24, BI_RGB };
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bh + stride * h, 0, 0, sizeof fh + sizeof bh };
    FILE* f = fopen(file, "wb");
    if (!f) return;
    fwrite(&fh, sizeof fh, 1, f);
    fwrite(&bh, sizeof bh, 1, f);
    fwrite(bgr, stride * h, 1, f);
    fclose(f);
    printf("[shot] %dx%d -> %s\n", w, h, file);
}

static void on_present(void* surf, int version) {
    LONG due = g_shot_due;
    if (due < 0 && !g_record[0]) return;
    typedef HRESULT (WINAPI *lock_t)(void*, RECT*, void*, DWORD, HANDLE);
    typedef HRESULT (WINAPI *unlock_t)(void*, void*);
    void** vt = *(void***)surf;
    uint8_t d[0x7C] = { 0 };                    /* DDSURFACEDESC (0x6C) or DDSURFACEDESC2 (0x7C) */
    *(DWORD*)d = version == 1 ? 0x6C : 0x7C;
    if (((lock_t)vt[25])(surf, NULL, d, 1 /* DDLOCK_WAIT */, NULL)) return;
    int w = *(int*)(d + 0x0C), h = *(int*)(d + 0x08), pitch = *(int*)(d + 0x10);
    int bits = (int)*(uint32_t*)(d + 0x54);
    uint32_t rmask = *(uint32_t*)(d + 0x58);
    const uint8_t* src = *(const uint8_t**)(d + 0x24);
    if (due >= 0) {
        DWORD stride = (w * 3 + 3) & ~3u;
        uint8_t* out = (uint8_t*)calloc(stride, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t* o = out + (h - 1 - y) * stride + x * 3;
                if (bits == 16) {
                    uint16_t p = ((const uint16_t*)(src + y * pitch))[x];
                    int r5 = rmask == 0x7C00 ? (p >> 10) & 31 : p >> 11;
                    int g = rmask == 0x7C00 ? ((p >> 5) & 31) << 3 : ((p >> 5) & 63) << 2;
                    o[0] = (uint8_t)((p & 31) << 3); o[1] = (uint8_t)g; o[2] = (uint8_t)(r5 << 3);
                } else if (bits == 32) {
                    memcpy(o, src + y * pitch + x * 4, 3);
                }
            }
        write_bmp(g_shot_file[due], w, h, out, stride);
        free(out);
        InterlockedExchange(&g_shot_due, -1);
    }
    if (g_record[0] && (bits == 16 || bits == 32)) {
        if (!g_rec) {
            char cmd[MAX_PATH + 256];
            _snprintf(cmd, sizeof cmd - 1, "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt %s -s %dx%d "
                      "-use_wallclock_as_timestamps 1 -i - -vf format=yuv420p -fps_mode vfr \"%s\"",
                      bits == 32 ? "bgr0" : rmask == 0x7C00 ? "rgb555le" : "rgb565le", w, h, g_record);
            cmd[sizeof cmd - 1] = 0;
            g_rec = _popen(cmd, "wb");
            g_rec_w = w; g_rec_h = h; g_rec_bits = bits;
            if (!g_rec) { fprintf(stderr, "[record] cannot start ffmpeg\n"); g_record[0] = 0; }
        }
        if (g_rec && w == g_rec_w && h == g_rec_h && bits == g_rec_bits)
            for (int y = 0; y < h; y++) fwrite(src + y * pitch, (size_t)(w * bits / 8), 1, g_rec);
    }
    ((unlock_t)vt[32])(surf, NULL);
}

/* The shell draws with GDI: an 8-bit DIB section StretchBlt'd to the window
 * (no DirectDraw surface to hand to on_present). Frames for --shot and
 * --record are read from the source DIB and its colour table. */
static HBITMAP g_gdi_bmp;      /* the last DIB blitted to the window, and its colours */
static RGBQUAD g_gdi_pal[256];
static RECT g_gdi_src;         /* the part of it blitted (the sim's DIB is larger than its view) */

static void gdi_frame(LONG due) {
    DIBSECTION ds;
    RGBQUAD* pal = g_gdi_pal;
    if (due < 0 && !g_record[0]) return;
    if (!g_gdi_bmp || GetObjectA(g_gdi_bmp, sizeof ds, &ds) != sizeof ds || !ds.dsBm.bmBits) return;
    int pitch = ds.dsBm.bmWidthBytes, x0 = 0, y0 = 0, w = ds.dsBm.bmWidth, h = ds.dsBm.bmHeight;
    if (g_gdi_src.right > g_gdi_src.left && g_gdi_src.right <= w && g_gdi_src.bottom <= h) {
        x0 = g_gdi_src.left; y0 = g_gdi_src.top;
        w = g_gdi_src.right - x0; h = g_gdi_src.bottom - y0;
    }
    /* ponytail: top-down, as both modules make it (biHeight -480; GetObject
     * reports it positive). Read the BITMAPINFO if another DIB turns up. */
    int bottom_up = 0;
    uint32_t* rgb = (uint32_t*)malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = (const uint8_t*)ds.dsBm.bmBits + (size_t)(bottom_up ? h - 1 - y : y0 + y) * pitch + x0;
        for (int x = 0; x < w; x++) rgb[y * w + x] = *(uint32_t*)&pal[row[x]];
    }
    if (due >= 0) {
        DWORD stride = (w * 3 + 3) & ~3u;
        uint8_t* out = (uint8_t*)calloc(stride, h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) memcpy(out + (h - 1 - y) * stride + x * 3, &rgb[y * w + x], 3);
        write_bmp(g_shot_file[due], w, h, out, stride);
        free(out);
        InterlockedExchange(&g_shot_due, -1);
    }
    if (g_record[0]) {
        if (!g_rec) {
            char cmd[MAX_PATH + 256];
            _snprintf(cmd, sizeof cmd - 1, "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt bgr0 -s %dx%d "
                      "-use_wallclock_as_timestamps 1 -i - -vf format=yuv420p -fps_mode vfr \"%s\"", w, h, g_record);
            cmd[sizeof cmd - 1] = 0;
            g_rec = _popen(cmd, "wb");
            g_rec_w = w; g_rec_h = h; g_rec_bits = 32;
            if (!g_rec) { fprintf(stderr, "[record] cannot start ffmpeg\n"); g_record[0] = 0; }
        }
        if (g_rec && w == g_rec_w && h == g_rec_h) fwrite(rgb, (size_t)w * h * 4, 1, g_rec);
    }
    free(rgb);
}

/* --skip-movies: every `play <alias> ... notify` the shell sends MCI ends at
 * once (MM_MCINOTIFY, successful) without playing. The Indeo 3 movies have
 * no decoder on current Windows anyway, and the intro runs 87 s. */
static int g_skip_movies;

static MCIERROR WINAPI my_mciSendStringA(LPCSTR cmd, LPSTR ret, UINT n, HWND cb) {
    if (g_skip_movies && !_strnicmp(cmd, "play ", 5) && strstr(cmd, "notify")) {
        char alias[64] = "";
        sscanf(cmd + 5, "%63s", alias);
        printf("[movie] skipped: %s\n", cmd);
        PostMessageA(cb, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, (LPARAM)mciGetDeviceIDA(alias));
        return 0;
    }
    return mciSendStringA(cmd, ret, n, cb);
}

/* The shell blits only on WM_PAINT, so a shot reads the last frame. */
static void on_gdi_present(HDC src, int sx, int sy, int sw, int sh) {
    SetRect(&g_gdi_src, sx, sy, sx + sw, sy + sh);
    HBITMAP b = (HBITMAP)GetCurrentObject(src, OBJ_BITMAP);
    DIBSECTION ds;
    if (GetObjectA(b, sizeof ds, &ds) != sizeof ds || ds.dsBm.bmBitsPixel != 8) return;
    GetDIBColorTable(src, 0, 256, g_gdi_pal);
    g_gdi_bmp = b;
    gdi_frame(-1);
}

static BOOL WINAPI my_StretchBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, int sw, int sh, DWORD rop) {
    BOOL r = StretchBlt(d, x, y, w, h, s, sx, sy, sw, sh, rop);
    on_gdi_present(s, sx, sy, sw, sh);
    return r;
}

static BOOL WINAPI my_BitBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, DWORD rop) {
    BOOL r = BitBlt(d, x, y, w, h, s, sx, sy, rop);
    if (s) on_gdi_present(s, sx, sy, w, h);
    return r;
}

/* ponytail: a 200 ms grace for the frame in flight, not a lock. */
static void record_close(void) {
    if (!g_rec) return;
    g_record[0] = 0;
    Sleep(200);
    _pclose(g_rec);
    g_rec = NULL;
}

static DWORD WINAPI shot_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_shots; i++) {
        DWORD at = t0 + g_shot_s[i] * 1000, now = GetTickCount();
        if (at > now) Sleep(at - now);
        if (g_gdi_bmp) { gdi_frame(i); continue; }
        InterlockedExchange(&g_shot_due, i);
        for (int k = 0; k < 150 && g_shot_due >= 0; k++) Sleep(20);
        if (InterlockedExchange(&g_shot_due, -1) >= 0) printf("[shot] no frame presented in 3 s\n");
    }
    return 0;
}

/* ---- --key VK@S and --click X,Y@S: input posted to the game window --------
 * Posted, not SendInput: under --headless the window is on a desktop of its
 * own, and SendInput would type into the user's session. */
static struct { int x, y; WORD vk; DWORD at, hold; } g_input[64];
static volatile BYTE g_keydown[256];   /* --key state, for GetKeyState under --headless */
static int g_ninput;
static HWND game_window(void);
/* The cursor the game sees: GetCursorPos fails on --headless's private
 * desktop (nothing has the input), so it answers the last --click's point,
 * and SetCursorPos moves it. */
static POINT g_cursor = { 320, 240 };
static int g_cursor_client = 1;    /* g_cursor is in client coordinates */

static BOOL WINAPI my_GetCursorPos(POINT* p) {
    *p = g_cursor;
    if (g_cursor_client) ClientToScreen(game_window(), p);
    return TRUE;
}

static SHORT WINAPI my_GetKeyState(int vk) {
    return g_keydown[vk & 0xFF] ? (SHORT)0x8000 : GetKeyState(vk);
}

static BOOL WINAPI my_SetCursorPos(int x, int y) {
    g_cursor.x = x; g_cursor.y = y; g_cursor_client = 0;
    return TRUE;
}


static DWORD WINAPI input_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_ninput; i++) {
        DWORD at = t0 + g_input[i].at, now = GetTickCount();
        if (at > now) Sleep(at - now);
        HWND w = game_window();
        if (!w) { printf("[input] no window\n"); continue; }
        /* Both ways in: the game's menus and park read DirectInput (native32's
         * synthetic devices under --headless), its text boxes WM_CHAR. The
         * mouse is relative, so a click first pins it to the top-left corner
         * and then moves it by X,Y; the game's cursor follows its deltas. */
        if (g_input[i].vk) {
            UINT sc = MapVirtualKeyA(g_input[i].vk, 0);
            LPARAM lp = 1 | ((LPARAM)sc << 16);
            native32_input_key((int)sc, 1);
            g_keydown[g_input[i].vk & 0xFF] = 1;
            PostMessageA(w, WM_KEYDOWN, g_input[i].vk, lp);
            Sleep(g_input[i].hold);
            g_keydown[g_input[i].vk & 0xFF] = 0;
            native32_input_key((int)sc, 0);
            PostMessageA(w, WM_KEYUP, g_input[i].vk, lp | 0xC0000000);
            printf("[key] 0x%02X at %lu ms\n", g_input[i].vk, GetTickCount() - t0);
        } else {
            native32_input_mouse(-4000, -4000, 0);
            Sleep(150);
            native32_input_mouse(g_input[i].x, g_input[i].y, 0);
            g_cursor.x = g_input[i].x; g_cursor.y = g_input[i].y; g_cursor_client = 1;
            LPARAM xy = MAKELPARAM(g_input[i].x, g_input[i].y);
            PostMessageA(w, WM_MOUSEMOVE, 0, xy);
            Sleep(250);
            native32_input_button(0, 1);
            PostMessageA(w, WM_LBUTTONDOWN, MK_LBUTTON, xy);
            Sleep(200);
            native32_input_button(0, 0);
            PostMessageA(w, WM_LBUTTONUP, 0, xy);
            printf("[click] %d,%d at %lu ms\n", g_input[i].x, g_input[i].y, GetTickCount() - t0);
        }
    }
    return 0;
}

static int add_input(const char* spec, int click) {
    const char* at = strchr(spec, '@');
    if (!at || g_ninput >= 64) return 0;
    if (click) {
        if (sscanf(spec, "%d,%d", &g_input[g_ninput].x, &g_input[g_ninput].y) != 2) return 0;
    } else {
        g_input[g_ninput].vk = (WORD)strtoul(spec, NULL, 0);
    }
    {   /* VK@S+HOLD: held HOLD seconds (default 0.15) */
        const char* plus = strchr(at, '+');
        g_input[g_ninput].hold = plus ? (DWORD)(atof(plus + 1) * 1000) : 150;
    }
    g_input[g_ninput++].at = (DWORD)(atof(at + 1) * 1000);
    return 1;
}

/* ---- --play: ES.EXE's loop ----------------------------------------------
 * ES.EXE kept a state, 1 at start, ran a child for it and took the child's
 * exit code as the next state until it was 0: states 1, 3, 4 and 6 run
 * `vshell dummy -eggplant -X<state>`, 2 and 5 run `dbsim dummy -eggplant
 * -X<state> -R<n>` (n counts sim runs; 5 adds -D). The children here are
 * es2shell.exe and es2sim.exe beside this one, with this process's own host
 * options passed on after the game's (the game keeps only its first few
 * arguments); under --headless they inherit its private desktop. */
static int play(int argc, char** argv) {
    char dir[MAX_PATH], *slash, opts[2048] = "";
    int state = 1, runs = 0;
    /* The children die with this process (a killed test run left them
     * looping, and a second shell refuses to start beside the first). */
    HANDLE job = CreateJobObjectA(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim = { 0 };
    lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim);
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    if ((slash = strrchr(dir, '\\'))) *slash = 0;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--play") && strcmp(argv[i], "--headless") && strlen(opts) + strlen(argv[i]) + 4 < sizeof opts)
            strcat(strcat(strcat(opts, strchr(argv[i], ' ') ? " \"" : " "), argv[i]), strchr(argv[i], ' ') ? "\"" : "");
    SetEnvironmentVariableA("ES2_OPTS", opts);
    while (state > 0 && state <= 6) {
        int sim = state == 2 || state == 5;
        char cmd[4096];
        STARTUPINFOA si = { sizeof si };
        PROCESS_INFORMATION pi;
        DWORD rc = 0;
        if (sim)
            _snprintf(cmd, sizeof cmd - 1, "\"%s\\es2sim.exe\" dummy -eggplant -X%d -R%d%s --run",
                      dir, state, runs++, state == 5 ? " -D" : "");
        else
            _snprintf(cmd, sizeof cmd - 1, "\"%s\\es2shell.exe\" dummy -eggplant -X%d --run", dir, state);
        cmd[sizeof cmd - 1] = 0;
        printf("[play] state %d: %s (ES2_OPTS=%s)\n", state, cmd, opts);
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            fprintf(stderr, "[play] cannot start %s\n", cmd);
            return 1;
        }
        AssignProcessToJobObject(job, pi.hProcess);
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &rc);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        state = (int)rc;
    }
    printf("[play] done (state %d)\n", state);
    return 0;
}

static int host_main(int argc, char** argv) {
    const char* game = "game";
    int run = 0;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Silent test runs: --mute, and --headless unless PCRECOMP_MUTE says
     * otherwise (native32_init mutes the audio session; PCRECOMP_MUTE=0
     * restores it). Set before the relaunches, which inherit it. */
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--mute") ||
            (!strcmp(argv[i], "--headless") && !GetEnvironmentVariableA("PCRECOMP_MUTE", NULL, 0)))
            SetEnvironmentVariableA("PCRECOMP_MUTE", "1");
    for (int i = 1; i < argc; i++) {
        int rc;
        if (!strcmp(argv[i], "--headless") && (rc = native32_headless()) >= 0) return rc;
    }
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--play")) return play(argc, argv);
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--headless") || !strcmp(argv[i], "--mute")) ;
        else if (argv[i][0] != '-' || argv[i][1] != '-') ;   /* the game's own */
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && g_shots < 16) {
            g_shot_s[g_shots] = strtoul(argv[++i], NULL, 0);
            GetFullPathNameA(argv[++i], MAX_PATH, g_shot_file[g_shots++], NULL);
        }
        else if (!strcmp(argv[i], "--key") && i + 1 < argc) { if (!add_input(argv[++i], 0)) return 1; }
        else if (!strcmp(argv[i], "--click") && i + 1 < argc) { if (!add_input(argv[++i], 1)) return 1; }
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) GetFullPathNameA(argv[++i], MAX_PATH, g_record, NULL);
        else if (!strcmp(argv[i], "--skip-movies")) g_skip_movies = 1;
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else {
            printf("usage: es2shell --play [host options]            the whole game (ES.EXE's loop)\n"
                   "       %s --run [host options] [game switches]  one module\n"
                   "host options: [--headless] [--mute] [--game DIR] [--watchdog S] [--shot S FILE.bmp]\n"
                   "              [--record FILE.mp4] [--key VK@S[+HOLD]] [--click X,Y@S] [--native-trace] [--callbacks]\n",
                   ES2_EXE);
            recomp_trace_help();
            return argv[i][2] == 'h' ? 0 : 1;
        }
    }
    GetFullPathNameA(game, MAX_PATH, g_game_dir, NULL);
    {
        char exe[MAX_PATH];
        _snprintf(exe, sizeof exe - 1, "%s\\%s", g_game_dir, ES2_EXE);
        exe[MAX_PATH - 1] = 0;
        native32_hold_image(exe, EXE_BASE);
    }
    SetDllDirectoryA(g_game_dir);     /* cw3220, bwcc32, sos9503 */
    /* The shell parses MCI's "status mov window handle" answer with scanf,
     * not sscanf: cw3220 reads stdin, so give it an empty one (it ignores
     * the result) instead of a console that blocks. Before cw3220 loads:
     * it takes its handles at attach. */
    SetStdHandle(STD_INPUT_HANDLE, CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                               OPEN_EXISTING, 0, NULL));


    native32_redirect("StretchBlt", (void*)my_StretchBlt);
    native32_redirect("BitBlt", (void*)my_BitBlt);
    if (GetEnvironmentVariableA("NATIVE32_HEADLESS", NULL, 0)) {
        native32_redirect("GetCursorPos", (void*)my_GetCursorPos);
        native32_redirect("SetCursorPos", (void*)my_SetCursorPos);
        native32_redirect("GetKeyState", (void*)my_GetKeyState);
    }
    native32_redirect("mciSendStringA", (void*)my_mciSendStringA);
    native32_init();
    native32_allow_heap_code();
    native32_on_present(on_present);
    AddVectoredExceptionHandler(0, crash);
    printf("Earth Siege 2 recomp host (%s)\n  lifted functions in dispatch: %u\n", ES2_EXE, recomp_dispatch_count);
    if (native32_modules_map(g_game_dir, g_game_dir, g_mods, 1) != 1) {
        fprintf(stderr, "cannot map %s\\%s at 0x%08X\n", g_game_dir, ES2_EXE, EXE_BASE);
        return 1;
    }
    int missing = native32_modules_bind(g_shims, (int)(sizeof g_shims / sizeof g_shims[0]));
    if (missing) fprintf(stderr, "warning: %d imports unresolved\n", missing);
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(uintptr_t)(EXE_BASE + MEM32(EXE_BASE + 0x3C));
    uint32_t entry = EXE_BASE + nt->OptionalHeader.AddressOfEntryPoint;
    if (!run) {
        printf("\n(dry run: mapped and bound; --run enters 0x%08X)\n", entry);
        return missing ? 1 : 0;
    }
    if (!SetCurrentDirectoryA(g_game_dir)) { fprintf(stderr, "cannot enter %s\n", g_game_dir); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (g_shots) CloseHandle(CreateThread(NULL, 0, shot_thread, NULL, 0, NULL));
    if (g_ninput) CloseHandle(CreateThread(NULL, 0, input_thread, NULL, 0, NULL));
    if (!native32_attach_imports(EXE_BASE)) return 1;
    printf("  entering 0x%08X\n\n", entry);
    native32_call_guest(entry, 0, NULL);
    record_close();
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}

/* Lifted code runs on the host's C stack and nests deeply, so it gets 16 MB on
 * a thread of its own; the first thread is linked with 256 KB (CMakeLists.txt)
 * so its stack fits below the game image (native32_hold_image). */
static int g_argc;
static char** g_argv;
static DWORD WINAPI host_thread(LPVOID unused) { (void)unused; return (DWORD)host_main(g_argc, g_argv); }

int main(int argc, char** argv) {
    DWORD code = 1;
    /* --play hands its options to the children in ES2_OPTS, not on the
     * command line: the game reads that too, and with 30-odd arguments it
     * fails to start ("Unable to initialize Instance"). */
    static char opts[2048], *av[128];
    if (GetEnvironmentVariableA("ES2_OPTS", opts, sizeof opts)) {
        int n = 0;
        for (; n < argc && n < 64; n++) av[n] = argv[n];
        for (char* p = opts; n < 127;) {
            while (*p == ' ') p++;
            if (!*p) break;
            char q = *p == '"' ? *p++ : ' ';
            av[n++] = p;
            while (*p && *p != q) p++;
            if (*p) *p++ = 0;
        }
        av[n] = NULL;
        argc = n;
        argv = av;
    }
    g_argc = argc;
    g_argv = argv;
    HANDLE t = CreateThread(NULL, 16u << 20, host_thread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!t) return 1;
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &code);
    return (int)code;
}
