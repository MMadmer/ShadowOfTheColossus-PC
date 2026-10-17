// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: shader preparation window (see sotc_progress.h).
#include "sotc_progress.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace SotcProgress {

namespace {

std::atomic<u32> g_total{0};
std::atomic<u32> g_done{0};
std::atomic<bool> g_open{false};
std::chrono::steady_clock::time_point g_start{};

#ifdef _WIN32
std::thread g_thread;
std::atomic<HWND> g_hwnd{nullptr};
std::atomic<bool> g_thread_done{false};
HFONT g_title_font{};
HFONT g_text_font{};
int g_dpi = 96;

int Scale(int v) {
    return MulDiv(v, g_dpi, 96);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Draw into a memory bitmap first: no flicker at 30 updates per second.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(24, 26, 31));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    const u32 total = g_total.load();
    const u32 done = std::min(g_done.load(), total);
    const int margin = Scale(22);

    RECT title{margin, Scale(16), rc.right - margin, Scale(44)};
    SelectObject(mem, g_title_font);
    SetTextColor(mem, RGB(236, 236, 240));
    DrawTextW(mem, L"Shadow of the Colossus", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    wchar_t line[160];
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
    if (done > 8 && done < total && elapsed > 1.0) {
        const int left = static_cast<int>(elapsed * (total - done) / done + 0.5);
        wsprintfW(line, L"Подготовка шейдеров: %u из %u  (осталось ~%d с)", done, total, left);
    } else {
        wsprintfW(line, L"Подготовка шейдеров: %u из %u", done, total);
    }
    RECT text{margin, Scale(48), rc.right - margin, Scale(72)};
    SelectObject(mem, g_text_font);
    SetTextColor(mem, RGB(180, 184, 194));
    DrawTextW(mem, line, -1, &text, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT bar{margin, Scale(82), rc.right - margin, Scale(100)};
    HBRUSH track = CreateSolidBrush(RGB(48, 52, 62));
    FillRect(mem, &bar, track);
    DeleteObject(track);
    if (total) {
        RECT fill = bar;
        fill.right = bar.left + static_cast<LONG>(static_cast<u64>(bar.right - bar.left) * done / total);
        HBRUSH accent = CreateSolidBrush(RGB(214, 170, 92));
        FillRect(mem, &fill, accent);
        DeleteObject(accent);
    }

    RECT hint{margin, Scale(106), rc.right - margin, Scale(128)};
    SetTextColor(mem, RGB(120, 124, 134));
    DrawTextW(mem, L"Нужно один раз после новых сцен: дальше игра идёт без рывков.", -1, &hint,
              DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void WindowThreadBody();
void WindowThread() {
    WindowThreadBody();
    g_thread_done = true;
}
void WindowThreadBody() {
    // Only a warm-up that takes a while gets a window (a cached one is often under a second).
    while (g_open.load() &&
           std::chrono::steady_clock::now() - g_start < std::chrono::milliseconds(500)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!g_open.load()) {
        return;
    }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_WAIT));
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"SotcShaderProgress";
    RegisterClassW(&wc);

    g_title_font = CreateFontW(-Scale(17), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_text_font = CreateFontW(-Scale(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");

    const int w = Scale(520);
    const int h = Scale(140);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - w) / 2;
    const int y = work.top + (work.bottom - work.top - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, wc.lpszClassName,
                                L"Shadow of the Colossus — подготовка шейдеров",
                                WS_POPUP | WS_BORDER, x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) {
        return;
    }
    g_hwnd = hwnd;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, 1, 33, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_hwnd = nullptr;
    DeleteObject(g_title_font);
    DeleteObject(g_text_font);
}
#endif

bool Enabled() {
    const char* v = std::getenv("SOTC_SHADER_WINDOW");
    return !v || v[0] != '0';
}

} // namespace

void Begin(u32 total) {
    g_total = total;
    g_done = 0;
    g_start = std::chrono::steady_clock::now();
#ifdef _WIN32
    // A handful of pipelines load in well under a second: no window for those.
    if (total < 16 || !Enabled() || g_open.exchange(true)) {
        return;
    }
    g_thread_done = false;
    g_thread = std::thread(WindowThread);
#endif
}

void Update(u32 done) {
    g_done = done;
}

void End() {
#ifdef _WIN32
    if (!g_open.exchange(false)) {
        return;
    }
    g_done = g_total.load();
    // The window thread may still be waiting (it then returns by itself) or creating the window.
    if (g_thread.joinable()) {
        while (true) {
            if (HWND hwnd = g_hwnd.load()) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                break;
            }
            if (g_thread_done.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        g_thread.join();
    }
#endif
}

} // namespace SotcProgress
