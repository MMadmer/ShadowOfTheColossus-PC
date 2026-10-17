// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: shader loading screen (see sotc_progress.h).
#include "sotc_progress.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
// The key art PNG only; private copy of stb_image (the port does not build common/stb.cpp).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include <stb_image.h>
#endif

namespace SotcProgress {

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<u32> g_total{0};
std::atomic<u32> g_done{0};
std::atomic<u32> g_stage{0};
std::atomic<bool> g_open{false};
std::atomic<Clock::rep> g_stage_start{0};
Clock::time_point g_open_time{};

#ifdef _WIN32
std::thread g_thread;
std::atomic<HWND> g_hwnd{nullptr};
std::atomic<bool> g_thread_done{false};
HFONT g_title_font{};
HFONT g_big_font{};
HFONT g_text_font{};
HFONT g_small_font{};
int g_dpi = 96;
// The key art, scaled and darkened once to the window size.
HBITMAP g_background{};
// ETA smoothing: rate of the building stage, from a sliding window of samples.
double g_rate = 0.0;
u32 g_rate_done = 0;
Clock::time_point g_rate_time{};

int Scale(int v) {
    return MulDiv(v, g_dpi, 96);
}

/// The game's key art (sce_sys/pic1.png, else pic0.png) covering w x h, darkened towards the
/// bottom so the text stays readable. nullptr without the image.
HBITMAP LoadBackground(HDC dc, int w, int h) {
    const char* game = std::getenv("SOTC_GAME_DIR");
    if (!game) {
        return nullptr;
    }
    int iw = 0, ih = 0, comp = 0;
    stbi_uc* pixels = nullptr;
    for (const char* name : {"/sce_sys/pic1.png", "/sce_sys/pic0.png"}) {
        const std::string path = std::string{game} + name;
        pixels = stbi_load(path.c_str(), &iw, &ih, &comp, 4);
        if (pixels) {
            break;
        }
    }
    if (!pixels) {
        return nullptr;
    }
    // Source DIB (BGRA, top-down).
    BITMAPINFO src_info{};
    src_info.bmiHeader = {sizeof(BITMAPINFOHEADER), iw, -ih, 1, 32, BI_RGB};
    void* src_bits = nullptr;
    HBITMAP src = CreateDIBSection(dc, &src_info, DIB_RGB_COLORS, &src_bits, nullptr, 0);
    if (!src) {
        stbi_image_free(pixels);
        return nullptr;
    }
    auto* s = static_cast<u8*>(src_bits);
    for (int i = 0; i < iw * ih; ++i) {
        s[i * 4 + 0] = pixels[i * 4 + 2];
        s[i * 4 + 1] = pixels[i * 4 + 1];
        s[i * 4 + 2] = pixels[i * 4 + 0];
        s[i * 4 + 3] = 255;
    }
    stbi_image_free(pixels);

    BITMAPINFO dst_info{};
    dst_info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* dst_bits = nullptr;
    HBITMAP dst = CreateDIBSection(dc, &dst_info, DIB_RGB_COLORS, &dst_bits, nullptr, 0);
    if (!dst) {
        DeleteObject(src);
        return nullptr;
    }
    // Cover: scale to fill, crop the overflow evenly.
    const double scale = std::max(double(w) / iw, double(h) / ih);
    const int cw = static_cast<int>(w / scale), ch = static_cast<int>(h / scale);
    const int cx = (iw - cw) / 2, cy = (ih - ch) / 2;
    HDC src_dc = CreateCompatibleDC(dc);
    HDC dst_dc = CreateCompatibleDC(dc);
    HGDIOBJ old_src = SelectObject(src_dc, src);
    HGDIOBJ old_dst = SelectObject(dst_dc, dst);
    SetStretchBltMode(dst_dc, HALFTONE);
    SetBrushOrgEx(dst_dc, 0, 0, nullptr);
    StretchBlt(dst_dc, 0, 0, w, h, src_dc, cx, cy, cw, ch, SRCCOPY);
    GdiFlush();
    SelectObject(src_dc, old_src);
    SelectObject(dst_dc, old_dst);
    DeleteDC(src_dc);
    DeleteDC(dst_dc);
    DeleteObject(src);

    // Darken: 60% of the art at the top, 22% at the bottom (where the text is).
    auto* d = static_cast<u8*>(dst_bits);
    for (int y = 0; y < h; ++y) {
        const double t = double(y) / std::max(1, h - 1);
        const int f = static_cast<int>(256 * (0.60 - 0.38 * t * t));
        u8* row = d + size_t(y) * w * 4;
        for (int x = 0; x < w * 4; x += 4) {
            row[x + 0] = static_cast<u8>(row[x + 0] * f >> 8);
            row[x + 1] = static_cast<u8>(row[x + 1] * f >> 8);
            row[x + 2] = static_cast<u8>(row[x + 2] * f >> 8);
        }
    }
    return dst;
}

void FormatTime(wchar_t* out, size_t size, double seconds) {
    const int s = static_cast<int>(seconds + 0.5);
    if (s >= 60) {
        swprintf(out, size, L"осталось ~%d мин %02d с", s / 60, s % 60);
    } else {
        swprintf(out, size, L"осталось ~%d с", std::max(s, 1));
    }
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;

    // Draw into a memory bitmap first: no flicker at 30 updates per second.
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    if (g_background) {
        HDC bg_dc = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(bg_dc, g_background);
        BitBlt(mem, 0, 0, w, h, bg_dc, 0, 0, SRCCOPY);
        SelectObject(bg_dc, old);
        DeleteDC(bg_dc);
    } else {
        HBRUSH bg = CreateSolidBrush(RGB(18, 19, 23));
        FillRect(mem, &rc, bg);
        DeleteObject(bg);
    }
    SetBkMode(mem, TRANSPARENT);

    const u32 total = g_total.load();
    const u32 done = std::min(g_done.load(), total);
    const auto stage = static_cast<Stage>(g_stage.load());
    const int margin = Scale(40);

    RECT title{margin, Scale(30), w - margin, Scale(70)};
    SelectObject(mem, g_title_font);
    SetTextColor(mem, RGB(240, 238, 232));
    DrawTextW(mem, L"SHADOW OF THE COLOSSUS", -1, &title, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    // Bottom block: stage, percentage, bar, details, hint.
    const int bar_h = Scale(8);
    const int bar_y = h - Scale(96);
    const double fraction = total ? double(done) / total : 0.0;

    wchar_t percent[16];
    swprintf(percent, 16, L"%d%%", static_cast<int>(fraction * 100.0));
    RECT pct{margin, bar_y - Scale(58), w - margin, bar_y - Scale(8)};
    SelectObject(mem, g_big_font);
    SetTextColor(mem, RGB(232, 196, 120));
    DrawTextW(mem, percent, -1, &pct, DT_RIGHT | DT_SINGLELINE | DT_BOTTOM);

    const wchar_t* stage_text = stage == Stage::Reading ? L"Чтение кэша шейдеров"
                                                        : L"Компиляция шейдеров";
    RECT st{margin, bar_y - Scale(52), w - margin, bar_y - Scale(12)};
    SelectObject(mem, g_text_font);
    SetTextColor(mem, RGB(236, 234, 228));
    DrawTextW(mem, stage_text, -1, &st, DT_LEFT | DT_SINGLELINE | DT_BOTTOM);

    RECT bar{margin, bar_y, w - margin, bar_y + bar_h};
    HBRUSH track = CreateSolidBrush(RGB(58, 56, 52));
    FillRect(mem, &bar, track);
    DeleteObject(track);
    if (total) {
        RECT fill = bar;
        fill.right = bar.left + static_cast<LONG>((bar.right - bar.left) * fraction);
        HBRUSH accent = CreateSolidBrush(RGB(214, 170, 92));
        FillRect(mem, &fill, accent);
        DeleteObject(accent);
    }

    wchar_t details[160];
    const auto now = Clock::now();
    if (stage == Stage::Building && done > 0 && done < total) {
        // Rate over the last few seconds, smoothed: the first pipelines are not representative.
        const double dt = std::chrono::duration<double>(now - g_rate_time).count();
        if (dt >= 1.0) {
            const double rate = (done - g_rate_done) / dt;
            g_rate = g_rate > 0 ? g_rate * 0.6 + rate * 0.4 : rate;
            g_rate_done = done;
            g_rate_time = now;
        }
        wchar_t left[64] = L"";
        if (g_rate > 0.5) {
            FormatTime(left, 64, (total - done) / g_rate);
        }
        swprintf(details, 160, L"%u из %u     %ls", done, total, left);
    } else {
        swprintf(details, 160, L"%u из %u", done, total);
    }
    RECT det{margin, bar_y + bar_h + Scale(10), w - margin, bar_y + bar_h + Scale(34)};
    SelectObject(mem, g_small_font);
    SetTextColor(mem, RGB(200, 198, 192));
    DrawTextW(mem, details, -1, &det, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT hint{margin, bar_y + bar_h + Scale(34), w - margin, bar_y + bar_h + Scale(58)};
    SetTextColor(mem, RGB(150, 148, 142));
    DrawTextW(mem,
              L"Шейдеры собираются один раз. Следующие запуски быстрые, в игре без рывков "
              L"от компиляции.",
              -1, &hint, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
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

void WindowThreadBody() {
    // Only a warm-up that takes a while gets the screen (a cached one is often under a second).
    while (g_open.load() && Clock::now() - g_open_time < std::chrono::milliseconds(700)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!g_open.load()) {
        return;
    }
    HINSTANCE inst = GetModuleHandleW(nullptr);
    HDC screen = GetDC(nullptr);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSX);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_APPSTARTING));
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"SotcShaderProgress";
    RegisterClassW(&wc);

    const auto font = [](int size, int weight, const wchar_t* face) {
        return CreateFontW(-Scale(size), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                           CLEARTYPE_QUALITY, 0, face);
    };
    g_title_font = font(26, FW_LIGHT, L"Segoe UI Light");
    g_big_font = font(40, FW_LIGHT, L"Segoe UI Light");
    g_text_font = font(19, FW_NORMAL, L"Segoe UI");
    g_small_font = font(14, FW_NORMAL, L"Segoe UI");

    // 16:9, about two thirds of the screen.
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int ww = work.right - work.left, wh = work.bottom - work.top;
    int w = std::max(ww * 2 / 3, std::min(ww, Scale(720)));
    int h = w * 9 / 16;
    if (h > wh * 9 / 10) {
        h = wh * 9 / 10;
        w = h * 16 / 9;
    }
    const int x = work.left + (ww - w) / 2;
    const int y = work.top + (wh - h) / 2;
    g_background = LoadBackground(screen, w, h);
    ReleaseDC(nullptr, screen);

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, wc.lpszClassName,
                                L"Shadow of the Colossus — компиляция шейдеров", WS_POPUP, x, y,
                                w, h, nullptr, nullptr, inst, nullptr);
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
    for (HFONT f : {g_title_font, g_big_font, g_text_font, g_small_font}) {
        DeleteObject(f);
    }
    if (g_background) {
        DeleteObject(g_background);
        g_background = nullptr;
    }
}

void WindowThread() {
    WindowThreadBody();
    g_thread_done = true;
}
#endif

bool Enabled() {
    const char* v = std::getenv("SOTC_SHADER_WINDOW");
    return !v || v[0] != '0';
}

} // namespace

void Begin(u32 total, Stage stage) {
    g_total = total;
    g_done = 0;
    g_stage = static_cast<u32>(stage);
    g_stage_start = Clock::now().time_since_epoch().count();
#ifdef _WIN32
    g_rate = 0.0;
    g_rate_done = 0;
    g_rate_time = Clock::now();
    // A handful of pipelines load in well under a second: no screen for those.
    if (total < 16 || !Enabled() || g_open.load()) {
        return;
    }
    g_open = true;
    g_open_time = Clock::now();
    g_thread_done = false;
    g_thread = std::thread(WindowThread);
#endif
}

void Update(u32 done) {
    // Workers finish out of order: keep the largest count.
    u32 current = g_done.load(std::memory_order_relaxed);
    while (done > current && !g_done.compare_exchange_weak(current, done)) {
    }
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
