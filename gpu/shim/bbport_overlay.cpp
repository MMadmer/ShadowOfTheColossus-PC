// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbport_overlay.h"

#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>

#include <SDL3/SDL.h>
#include "bbport_settings.h"
#include "sdl_window.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "stb_image.h"
#include <unordered_map>
#include "imgui_impl_vulkan.h"
#include "video_core/renderer_vulkan/vk_framegen.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/sotc_fps_cap.h"
#include "video_core/sotc_live.h"
#include "video_core/sotc_ui_lang.h"
#include "video_core/sotc_stats.h"

extern Frontend::WindowSDL* g_window;
static const Vulkan::Instance* s_instance = nullptr;
// sotc: the volume of the runtime's audio output (src/runtime_audio.c); weak, so builds without the
// runtime (tests) still link.
extern "C" __attribute__((weak)) void runtime_audio_set_volume_percent(int pct);
extern "C" __attribute__((weak)) int runtime_audio_volume_percent(void);

// DejaVu Sans (Cyrillic), embedded (third_party/fonts, Bitstream Vera license).
#ifdef _WIN32
asm(".section .rdata,\"dr\"\n"
    ".balign 16\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".text\n");
#else
asm(".section .rodata\n"
    ".balign 16\n"
    ".hidden bb_font_ttf\n"
    ".global bb_font_ttf\n"
    "bb_font_ttf:\n"
    ".incbin \"" BB_FONT_PATH "\"\n"
    ".hidden bb_font_ttf_end\n"
    ".global bb_font_ttf_end\n"
    "bb_font_ttf_end:\n"
    ".previous\n");
#endif
extern "C" const unsigned char bb_font_ttf[];
extern "C" const unsigned char bb_font_ttf_end[];

extern "C" void runtime_restart(void); // bb-probe (probe.c)

namespace BbOverlay {

namespace {

std::mutex imgui_mutex; // the ImGui context: window thread (input) and present thread
bool initialized = false;
std::atomic<bool> menu_open{false};
bool l3_down = false, r3_down = false;
bool dirty = false; // settings changed while open: saved on close
// The game's text dialog (SetTextEntry), guarded by imgui_mutex.
bool text_entry_active = false;
std::string text_entry_prompt, text_entry_text;
// The game's save/load slot list (OpenSaveList), guarded by imgui_mutex.
bool list_active = false;
std::string list_title;
std::vector<std::string> list_items;
// sotc: each save's picture (icon0.png of its sce_sys), shown to the left of its text as in the
// PS4 and shadPS4 lists. Loaded once per file, kept for the session.
std::vector<std::string> list_icons, pending_icons;
std::unordered_map<std::string, ImTextureData*> icon_textures;

ImTextureData* IconTexture(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    if (const auto it = icon_textures.find(path); it != icon_textures.end()) {
        return it->second;
    }
    ImTextureData* tex = nullptr;
    int w = 0, h = 0, n = 0;
    if (unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &n, 4)) {
        tex = IM_NEW(ImTextureData)();
        tex->Create(ImTextureFormat_RGBA32, w, h);
        std::memcpy(tex->GetPixels(), pixels, size_t(w) * h * 4);
        stbi_image_free(pixels);
        ImGui::RegisterUserTexture(tex);
    }
    icon_textures[path] = tex;
    return tex;
}
int list_selected = 0, list_result = -1;
int list_confirm = -1;  // item waiting for the delete confirmation, or -1
int list_first_deletable = 0;
bool list_scroll = false, list_stick_down = false, list_stick_up = false;
float base_scale = 1.0f;

// Present rate for the FPS counter.
std::chrono::steady_clock::time_point last_present{};
float frame_ms_avg = 0.0f;

// The settings menu (Insert / F1 / L3+R3); BB_OVERLAY_MENU=0 turns it off.
bool MenuAllowed() {
    const char* v = std::getenv("BB_OVERLAY_MENU");
    if (v && v[0] == '0') return false;
    return true;
}

// sotc: writes a whole file through a new one that then replaces it: a failed or cut write
// leaves the old file. While another program holds the file open (the launcher reading it, an
// antivirus scan) the replacement is refused for a moment: tried again, then written in place.
bool WriteWholeFile(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::path temp = path;
    temp += ".new";
    std::error_code error;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (out.fail()) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::filesystem::rename(temp, path, error);
        if (!error) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::filesystem::remove(temp, error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    return !out.fail();
}

// sotc: BbSettings::Save() writes a whole file, and sotc.ini (BB_CONFIG) also holds the
// launcher's key bindings, the control remaps and sotc.py's settings: the lines it does not
// write are kept.
void SaveSettings() {
    const char* env = std::getenv("BB_CONFIG");
    const std::string path = env && env[0] ? env : "bbport.ini";
    const std::string fresh = path + ".menu";
    const auto read_lines = [](const std::string& file) {
        std::vector<std::string> lines;
        std::ifstream in(file);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }
        return lines;
    };
    const auto key_of = [](const std::string& line) { return line.substr(0, line.find('=')); };
    BbSettings::Save(fresh.c_str());
    const std::vector<std::string> own_lines = read_lines(fresh);
    std::error_code error;
    std::filesystem::remove(fresh, error);
    if (own_lines.empty()) {
        return; // not written
    }
    // Also the other names Load() takes for the settings Save() writes.
    std::unordered_set<std::string> written{"mouse_enabled", "mouse_sensitivity"};
    std::string text;
    for (const std::string& line : own_lines) {
        if (line.find('=') != std::string::npos) {
            written.insert(key_of(line));
        }
        text += line + '\n';
    }
    for (const std::string& line : read_lines(path)) {
        const bool own = line.find('=') != std::string::npos ? written.contains(key_of(line))
                                                             : line.starts_with("# bbport settings");
        if (!line.empty() && !own) {
            text += line + '\n';
        }
    }
    if (!WriteWholeFile(path, text)) {
        std::printf("Settings: cannot write %s\n", path.c_str());
    }
}

void SetOpen(bool value) {
    if (menu_open.exchange(value) == value) {
        return;
    }
    ImGui::GetIO().MouseDrawCursor = value;
    if (!value && dirty) {
        dirty = false;
        SaveSettings();
    }
}

ImGuiKey KeyFromSdl(SDL_Keycode key) {
    switch (key) {
    case SDLK_TAB: return ImGuiKey_Tab;
    case SDLK_LEFT: return ImGuiKey_LeftArrow;
    case SDLK_RIGHT: return ImGuiKey_RightArrow;
    case SDLK_UP: return ImGuiKey_UpArrow;
    case SDLK_DOWN: return ImGuiKey_DownArrow;
    case SDLK_PAGEUP: return ImGuiKey_PageUp;
    case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
    case SDLK_HOME: return ImGuiKey_Home;
    case SDLK_END: return ImGuiKey_End;
    case SDLK_DELETE: return ImGuiKey_Delete;
    case SDLK_BACKSPACE: return ImGuiKey_Backspace;
    case SDLK_SPACE: return ImGuiKey_Space;
    case SDLK_RETURN: return ImGuiKey_Enter;
    case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
    case SDLK_ESCAPE: return ImGuiKey_Escape;
    case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
    case SDLK_RCTRL: return ImGuiKey_RightCtrl;
    case SDLK_LSHIFT: return ImGuiKey_LeftShift;
    case SDLK_RSHIFT: return ImGuiKey_RightShift;
    case SDLK_LALT: return ImGuiKey_LeftAlt;
    case SDLK_RALT: return ImGuiKey_RightAlt;
    default: return ImGuiKey_None;
    }
}

ImGuiKey KeyFromGamepad(u8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return ImGuiKey_GamepadFaceDown;
    case SDL_GAMEPAD_BUTTON_EAST: return ImGuiKey_GamepadFaceRight;
    case SDL_GAMEPAD_BUTTON_WEST: return ImGuiKey_GamepadFaceLeft;
    case SDL_GAMEPAD_BUTTON_NORTH: return ImGuiKey_GamepadFaceUp;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return ImGuiKey_GamepadDpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ImGuiKey_GamepadDpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ImGuiKey_GamepadDpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ImGuiKey_GamepadDpadRight;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ImGuiKey_GamepadL1;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ImGuiKey_GamepadR1;
    case SDL_GAMEPAD_BUTTON_START: return ImGuiKey_GamepadStart;
    case SDL_GAMEPAD_BUTTON_BACK: return ImGuiKey_GamepadBack;
    default: return ImGuiKey_None;
    }
}

// sotc: a short message at the top of the screen (F8: the frame generation mode): its text
// (under imgui_mutex) and until when, in steady clock milliseconds.
std::string toast_text;
std::atomic<long long> toast_until_ms{0};

long long NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool ToastShown() {
    return NowMs() < toast_until_ms.load(std::memory_order_relaxed);
}

// sotc: the performance overlay (show_fps=1 in sotc.ini, or SOTC_FPS / SOTC_OVERLAY=1; F10
// toggles it). History of present intervals (one per game flip), present thread only.
constexpr int kHistory = 240;
float frame_hist[kHistory]{};
int hist_pos = 0, hist_count = 0;
uint64_t session_frames = 0, session_late = 0, session_stutters = 0;
std::atomic<bool> overlay_on{[] {
    if (const char* v = std::getenv("SOTC_OVERLAY")) {
        return v[0] != '0';
    }
    return std::getenv("SOTC_FPS") != nullptr;
}()};

// Frame time the game aims for: 33.3 ms (30 FPS) or 16.7 ms (60 FPS), from the median.
float TargetMs() {
    if (hist_count < 8) {
        return 33.3f;
    }
    float sorted[kHistory];
    std::copy(frame_hist, frame_hist + hist_count, sorted);
    std::nth_element(sorted, sorted + hist_count / 2, sorted + hist_count);
    return sorted[hist_count / 2] < 25.0f ? 16.7f : 33.3f;
}

bool OverlayOn() {
    // sotc: SOTC_OVERLAY (the launcher's choice) wins over show_fps=1 in sotc.ini.
    static const bool chosen = std::getenv("SOTC_OVERLAY") != nullptr;
    return (!chosen && BbSettings::Get().show_fps) || overlay_on.load(std::memory_order_relaxed);
}

void RecordFrame(float ms) {
    frame_hist[hist_pos] = ms;
    hist_pos = (hist_pos + 1) % kHistory;
    hist_count = std::min(hist_count + 1, kHistory);
    ++session_frames;
    if (ms > TargetMs() * 1.1f) {
        ++session_late;
    }
    if (ms > 50.0f) {
        ++session_stutters;
    }
}

float PixelDensity(SDL_WindowID id) {
    SDL_Window* window = SDL_GetWindowFromID(id);
    const float density = window ? SDL_GetWindowPixelDensity(window) : 1.0f;
    return density > 0.0f ? density : 1.0f;
}

// Marks the settings dirty when a widget changed them.
template <typename T>
void Store(std::atomic<T>& target, T value, bool changed) {
    if (changed) {
        target = value;
        dirty = true;
    }
}

void Checkbox(const char* label, std::atomic<bool>& value) {
    bool v = value;
    Store(value, v, ImGui::Checkbox(label, &v));
}

void Slider(const char* label, std::atomic<float>& value, float lo, float hi) {
    float v = value;
    Store(value, v, ImGui::SliderFloat(label, &v, lo, hi, "%.2f"));
}

void Hint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// sotc: the launcher's settings file (SOTC_LAUNCHER_SETTINGS, user/launcher_settings.json), so a
// setting changed in the menu is also the launcher's choice next time. Only keys already in the
// file are replaced. Empty: the game was not started by the launcher.
std::filesystem::path LauncherSettingsPath() {
#ifdef _WIN32
    const wchar_t* path = _wgetenv(L"SOTC_LAUNCHER_SETTINGS");
#else
    const char* path = std::getenv("SOTC_LAUNCHER_SETTINGS");
#endif
    return path && path[0] ? std::filesystem::path{path} : std::filesystem::path{};
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::regex LauncherKey(const char* key) {
    return std::regex("(\"" + std::string(key) + "\"\\s*:\\s*)(\"[^\"]*\"|true|false|-?[0-9.]+)");
}

/// A key's JSON value in the launcher's settings file (true, "30", ...), or empty.
std::string LauncherSetting(const char* key) {
    const std::string text = ReadText(LauncherSettingsPath());
    std::smatch m;
    return std::regex_search(text, m, LauncherKey(key)) ? m[2].str() : std::string{};
}

void SaveLauncherSetting(const char* key, const std::string& json_value) {
    const std::filesystem::path path = LauncherSettingsPath();
    if (path.empty()) {
        return;
    }
    std::string text = ReadText(path);
    std::smatch m;
    if (!std::regex_search(text, m, LauncherKey(key))) {
        return;
    }
    WriteWholeFile(path, m.prefix().str() + m[1].str() + json_value + m.suffix().str());
}

// sotc: "Quick settings": the launcher's quick settings that can change while playing.
void QuickSettings() {
    using SotcUi::T;
    ImGui::SeparatorText(T("Ajuste rápido (vale na hora)", "Quick settings (apply at once)"));

    bool uncapped = SotcFpsCap::uncapped.load(std::memory_order_relaxed);
    if (ImGui::Checkbox(T("60 FPS (sem o limite de 30)  [F7]", "60 FPS (no 30 FPS cap)  [F7]"),
                        &uncapped)) {
        SotcFpsCap::Toggle();
        SaveLauncherSetting("fps_mode", uncapped ? "\"60\"" : "\"30\"");
    }

    using Vulkan::FrameGen::Mode;
    const Mode fg = Vulkan::FrameGen::Current();
    const char* fg_names[] = {T("Desligada", "Off"), "NVIDIA DLSS-G", "AMD FSR 3.1"};
    const char* fg_keys[] = {"\"off\"", "\"dlss\"", "\"fsr\""};
    if (ImGui::BeginCombo(T("Geração de quadros  [F8]", "Frame generation  [F8]"),
                          fg_names[static_cast<int>(fg)])) {
        for (int i = 0; i < 3; ++i) {
            const Mode mode = static_cast<Mode>(i);
            const bool available = Vulkan::FrameGen::Available(mode);
            if (!available) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Selectable(fg_names[i], mode == fg) && available &&
                Vulkan::FrameGen::Set(mode)) {
                SaveLauncherSetting("framegen", fg_keys[i]);
            }
            if (!available) {
                ImGui::EndDisabled();
            }
        }
        ImGui::EndCombo();
    }
    // sotc: multi frame generation (RTX 50): DLSS-G shows up to 4 frames per game frame.
    if (const u32 max_shown = SotcStats::fg_max_generated.load(std::memory_order_relaxed) + 1;
        fg == Mode::Dlss && max_shown > 2) {
        static constexpr const char* multipliers[] = {"x2", "x3", "x4"};
        const u32 now = std::clamp(SotcStats::fg_multiplier.load(std::memory_order_relaxed), 2u,
                                   max_shown);
        if (ImGui::BeginCombo(T("Multiplicador de quadros", "Frame multiplier"),
                              multipliers[now - 2])) {
            for (u32 m = 2; m <= std::min(max_shown, 4u); ++m) {
                if (ImGui::Selectable(multipliers[m - 2], m == now)) {
                    SotcStats::fg_multiplier.store(m, std::memory_order_relaxed);
                    SaveLauncherSetting("dlssg_mult", "\"" + std::to_string(m) + "\"");
                }
            }
            ImGui::EndCombo();
        }
    }

    static const int env_linger = [] {
        const char* v = std::getenv("SOTC_LOD_LINGER");
        return v ? std::atoi(v) : 0;
    }();
    const int live_linger = SotcLive::lod_linger.load(std::memory_order_relaxed);
    bool hq = (live_linger >= 0 ? live_linger : env_linger) > 0;
    if (ImGui::Checkbox(T("Texturas em alta qualidade", "High quality textures"), &hq)) {
        SotcLive::lod_linger.store(hq ? 1 : 0, std::memory_order_relaxed);
        SaveLauncherSetting("hq_textures", hq ? "true" : "false");
    }
    Hint(T("Folhas e pedras nítidas. Se o jogo fechar em vistas muito amplas, desligue.",
           "Sharp leaves and rocks. If the game closes in very wide views, turn it off."));

    static const int env_reuse = [] {
        const char* v = std::getenv("SOTC_SHADOW_REUSE");
        if (!v) {
            return 0;
        }
        if (v[0] == 'a') {
            return s_instance && s_instance->GetVendorID() == 0x10DE ? 1 : 0;
        }
        return v[0] == '0' ? 0 : 1;
    }();
    const int live_reuse = SotcLive::shadow_reuse.load(std::memory_order_relaxed);
    bool reuse = (live_reuse >= 0 ? live_reuse : env_reuse) > 0;
    if (ImGui::Checkbox(T("Reaproveitar sombras distantes (podem piscar)",
                          "Reuse far shadows (they may blink)"),
                        &reuse)) {
        SotcLive::shadow_reuse.store(reuse ? 1 : 0, std::memory_order_relaxed);
        SaveLauncherSetting("shadow_reuse", reuse ? "\"1\"" : "\"0\"");
    }

    if (runtime_audio_volume_percent && runtime_audio_set_volume_percent) {
        static const int volumes[] = {100, 150, 200, 300};
        const int now = runtime_audio_volume_percent();
        char label[16];
        std::snprintf(label, sizeof(label), "%d%%", now);
        if (ImGui::BeginCombo(T("Volume", "Volume"), label)) {
            for (const int v : volumes) {
                char item[16];
                std::snprintf(item, sizeof(item), "%d%%", v);
                if (ImGui::Selectable(item, v == now)) {
                    runtime_audio_set_volume_percent(v);
                    SaveLauncherSetting("volume", "\"" + std::to_string(v) + "\"");
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", T("AF 16x, modo PS4 Pro, resolução e uso de memória de vídeo: no "
                                "lançador (valem ao abrir o jogo de novo).",
                                "16x AF, PS4 Pro mode, resolution and video memory use: in the "
                                "launcher (they apply when the game starts again)."));
    ImGui::PopStyleColor();
}

// Window and performance overlay, both switched at once.
void DisplaySettings() {
    using SotcUi::T;
    ImGui::SeparatorText(T("Tela", "Display"));
    if (g_window) {
        bool fs = g_window->IsFullscreen();
        if (ImGui::Checkbox(T("Tela cheia  [F11]", "Fullscreen  [F11]"), &fs)) {
            g_window->SetFullscreen(fs);
        }
        ImGui::SameLine();
    }
    // As F10: for this session (the launcher's choice holds at the next start).
    bool perf = OverlayOn();
    if (ImGui::Checkbox(T("Painel de desempenho  [F10]", "Performance overlay  [F10]"), &perf)) {
        BbSettings::Get().show_fps = false;
        overlay_on.store(perf, std::memory_order_relaxed);
    }
}

// sotc: why DLSS is not available, in the UI language: the renderer's reasons
// (vk_dlssg.cpp OnDeviceCreated); others as they are.
const char* DlssProblem(const char* problem) {
    using SotcUi::T;
    if (!problem) {
        return T("requer uma placa NVIDIA RTX e o DLSS ligado no lançador",
                 "it needs an NVIDIA RTX graphics card and DLSS turned on in the launcher");
    }
    const std::string_view reason{problem};
    if (reason == "NVIDIA Streamline not loaded") {
        return T("o NVIDIA Streamline não foi carregado", "NVIDIA Streamline not loaded");
    }
    if (reason == "not an NVIDIA RTX graphics card") {
        return T("a placa de vídeo não é uma NVIDIA RTX", "not an NVIDIA RTX graphics card");
    }
    if (reason == "not supported by this graphics card or driver") {
        return T("sem suporte nesta placa de vídeo ou neste driver",
                 "not supported by this graphics card or driver");
    }
    if (reason == "DLSS evaluation failed") {
        return T("o DLSS falhou nesta sessão (veja o registro)",
                 "DLSS failed in this session (see the log)");
    }
    return problem;
}

// sotc: DLSS Super Resolution (vk_dlssg.cpp): the renderer reads the choice every frame and
// stores the scene size it upscales from.
void Upscaling() {
    using SotcUi::T;
    auto& s = BbSettings::Get();
    ImGui::SeparatorText(T("Upscaling (DLSS)", "Upscaling (DLSS)"));
    const bool supported = s.dlss_supported.load(std::memory_order_relaxed);
    bool on = supported && s.upscaler.load(std::memory_order_relaxed) == BbSettings::UpscalerDlss;
    ImGui::BeginDisabled(!supported);
    if (ImGui::Checkbox(T("NVIDIA DLSS Super Resolution", "NVIDIA DLSS Super Resolution"), &on)) {
        s.upscaler.store(on ? BbSettings::UpscalerDlss : BbSettings::UpscalerOff,
                         std::memory_order_relaxed);
    }
    ImGui::EndDisabled();
    Hint(T("Liga e desliga na hora.", "Switches at once."));
    // sotc: on a GeForce RTX DLSS is only off for this session: the render size of a DLSS mode
    // is set at start, in the launcher.
    const char* problem = s.dlss_problem.load(std::memory_order_relaxed);
    const bool rtx = s_instance &&
                     std::string_view{s_instance->GetModelName()}.find("RTX") != std::string_view::npos;
    if (!supported && !problem && rtx) {
        ImGui::TextDisabled("%s", T("Escolha um modo DLSS no lançador.",
                                    "Choose a DLSS mode in the launcher."));
    } else if (!supported) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
        ImGui::TextWrapped(T("Indisponível: %s", "Unavailable: %s"),
                           DlssProblem(problem));
        ImGui::PopStyleColor();
    } else if (on) {
        ImGui::TextDisabled(T("Render %d x %d → saída", "Render %d x %d → output"),
                            s.active_render_width.load(std::memory_order_relaxed),
                            s.active_render_height.load(std::memory_order_relaxed));
    }
}

// sotc: the launcher's engine effects (EFFECTS and perf_dma in sotc_launcher_win.py), which it
// passes to the game at start (SOTC_CVARS, BB_DIRECT_MEMORY_ACCESS): a change goes to its
// settings file and applies when the game starts again.
void EngineEffects() {
    using SotcUi::T;
    static constexpr const char* keys[] = {"fx_vignette", "fx_fog", "fx_ssao", "perf_dma"};
    const char* const labels[std::size(keys)] = {
        T("Vinheta (cantos escurecidos)", "Vignette (darkened corners)"),
        T("Névoa atmosférica", "Atmospheric fog"),
        T("Oclusão ambiente (SSAO)", "Ambient occlusion (SSAO)"),
        T("Reflexos completos da água (acesso direto à memória)",
          "Full water reflections (direct memory access)"),
    };
    static const bool launcher = !LauncherSettingsPath().empty();
    // The launcher's choices, read once: on unless its file says false.
    static std::array<bool, std::size(keys)> values = [] {
        std::array<bool, std::size(keys)> on{};
        for (size_t i = 0; i < on.size(); ++i) {
            on[i] = LauncherSetting(keys[i]) != "false";
        }
        return on;
    }();
    ImGui::SeparatorText(T("Efeitos do jogo", "Engine effects"));
    ImGui::BeginDisabled(!launcher);
    for (size_t i = 0; i < values.size(); ++i) {
        if (ImGui::Checkbox(labels[i], &values[i])) {
            SaveLauncherSetting(keys[i], values[i] ? "true" : "false");
        }
    }
    ImGui::EndDisabled();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", launcher ? T("Gravados no lançador; valem ao abrir o jogo de novo.",
                                          "Saved in the launcher; they apply when the game starts "
                                          "again.")
                                      : T("Só pelo lançador (o jogo foi aberto sem ele).",
                                          "Only through the launcher (the game was started "
                                          "without it)."));
    ImGui::PopStyleColor();
}

void Menu() {
    using SotcUi::T;
    auto& s = BbSettings::Get();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + 40.0f * base_scale,
                                   viewport->WorkPos.y + 40.0f * base_scale),
                            ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(660.0f * base_scale, 0.0f), ImGuiCond_Appearing);
    bool keep_open = true;
    const std::string title = std::string("Shadow of the Colossus — ") +
                              T("Configurações", "Settings") + "  (Insert / F1 / L3+R3)###menu";
    if (!ImGui::Begin(title.c_str(), &keep_open,
                      ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // --- Header: FPS, GPU & VRAM info ---
    ImGui::Text(T("%.0f FPS  (%.1f ms)", "%.0f FPS  (%.1f ms)"),
                frame_ms_avg > 0.0f ? 1000.0f / frame_ms_avg : 0.0f, frame_ms_avg);
    if (s_instance) {
        const u64 budget_mb = s_instance->GetTotalMemoryBudget() / (1024 * 1024);
        const u64 used_mb = s_instance->GetDeviceMemoryUsage() / (1024 * 1024);
        ImGui::SameLine();
        ImGui::TextDisabled(T(" |  %s (%lu / %lu MB de VRAM)", " |  %s (%lu / %lu MB VRAM)"),
                            std::string(s_instance->GetModelName()).c_str(),
                            static_cast<unsigned long>(used_mb),
                            static_cast<unsigned long>(budget_mb));
    }

    QuickSettings();
    DisplaySettings();
    Upscaling();

    // --- Camera & Mouse Control (live: window.cpp bbgpu_mouse_take) ---
    ImGui::SeparatorText(T("Câmera e mouse", "Camera & mouse"));
    Checkbox(T("Câmera no mouse", "Mouse camera"), s.mouse_enabled);
    Hint(T("Vale na hora. Ao abrir o jogo pelo lançador, valem os ajustes de mouse dele.",
           "Applies at once. When the game starts from the launcher, its mouse settings are "
           "used."));
    ImGui::BeginDisabled(!s.mouse_enabled);
    Slider(T("Sensibilidade do mouse", "Mouse sensitivity"), s.mouse_sens, 0.1f, 3.0f);
    Checkbox(T("Inverter o eixo vertical", "Invert the vertical axis"), s.mouse_invert);
    ImGui::EndDisabled();

    EngineEffects();

    // --- Footer ---
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button(T("Salvar no sotc.ini", "Save to sotc.ini"))) {
        SaveSettings();
        dirty = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(T("Fechar", "Close"))) {
        keep_open = false;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", T("As configurações são salvas ao fechar", "Settings are saved on close"));

    ImGui::End();
    if (!keep_open) {
        SetOpen(false);
    }
}

// The game's text dialog: what is typed, and how to finish (keyboard or controller).
void TextEntryBox() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.42f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(viewport->WorkSize.x * 0.32f, 0.0f),
                                        ImVec2(viewport->WorkSize.x * 0.8f, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##text_entry", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.45f, 1.0f), "%s", text_entry_prompt.c_str());
    ImGui::Separator();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("%s_", text_entry_text.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::TextDisabled("%s", SotcUi::T("Teclado: digite, Backspace apaga, Enter = OK, Esc = cancelar",
                                        "Keyboard: type, Backspace to erase, Enter = OK, Esc = cancel"));
    ImGui::TextDisabled("%s", SotcUi::T("Controle: ✕ (A) = OK, ○ (B) = cancelar",
                                        "Controller: ✕ (A) = OK, ○ (B) = cancel"));
    ImGui::End();
}

// The game's save/load slot list: one entry per save, the selected one highlighted.
void SaveListBox() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.46f, viewport->WorkSize.y * 0.62f),
                             ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("##save_list", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.25f);
    ImGui::PushTextWrapPos(0.0f); // sotc: save dialog messages are whole sentences
    ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.45f, 1.0f), "%s", list_title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.2f;
    ImGui::BeginChild("##items", ImVec2(0.0f, -footer));
    // sotc: rows with the save's picture on the left (16:9 of 228x128) and its text on the right.
    const bool any_icon = std::any_of(list_icons.begin(), list_icons.end(),
                                      [](const std::string& p) { return !p.empty(); });
    const float row_h = any_icon ? ImGui::GetTextLineHeightWithSpacing() * 4.2f : 0.0f;
    for (int i = 0; i < static_cast<int>(list_items.size()); ++i) {
        ImGui::PushID(i);
        const bool selected = i == list_selected;
        ImTextureData* icon = i < static_cast<int>(list_icons.size()) ? IconTexture(list_icons[i]) : nullptr;
        const ImVec2 start = ImGui::GetCursorPos();
        const bool clicked =
            any_icon ? ImGui::Selectable("##row", selected,
                                         ImGuiSelectableFlags_AllowDoubleClick |
                                             ImGuiSelectableFlags_AllowOverlap,
                                         ImVec2(0.0f, row_h))
                     : ImGui::Selectable(list_items[i].c_str(), selected,
                                         ImGuiSelectableFlags_AllowDoubleClick);
        if (clicked) {
            list_selected = i;
            if (ImGui::IsMouseDoubleClicked(0)) {
                list_result = i;
            }
        }
        if (any_icon) {
            const ImVec2 end = ImGui::GetCursorPos();
            const float pad = row_h * 0.06f;
            const float img_h = row_h - pad * 2.0f;
            const float img_w = icon ? img_h * float(icon->Width) / float(std::max(icon->Height, 1))
                                     : img_h * 228.0f / 128.0f;
            ImGui::SetCursorPos(ImVec2(start.x + pad, start.y + pad));
            if (icon) {
                ImGui::Image(icon->GetTexRef(), ImVec2(img_w, img_h));
            } else {
                ImGui::Dummy(ImVec2(img_w, img_h));
            }
            const float text_x = start.x + pad * 3.0f + img_w;
            const float text_h = ImGui::CalcTextSize(list_items[i].c_str()).y;
            ImGui::SetCursorPos(ImVec2(text_x, start.y + std::max(pad, (row_h - text_h) * 0.5f)));
            ImGui::TextUnformatted(list_items[i].c_str());
            ImGui::SetCursorPos(end);
        }
        if (selected && list_scroll) {
            ImGui::SetScrollHereY(0.5f);
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    list_scroll = false;
    ImGui::EndChild();
    ImGui::Separator();
    using SotcUi::T;
    if (list_confirm >= 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                           T("Apagar este save?   Enter / ✕ (A): sim    Esc / ○ (B): não",
                             "Delete this save?   Enter / ✕ (A): yes    Esc / ○ (B): no"));
    } else if (list_first_deletable >= static_cast<int>(list_items.size())) { // sotc: YES/NO
        ImGui::TextDisabled("%s", T("Setas / direcional: escolher    Enter / ✕ (A): confirmar    "
                                    "Esc / ○ (B): não",
                                    "Arrows / D-pad: select    Enter / ✕ (A): confirm    "
                                    "Esc / ○ (B): no"));
    } else {
        ImGui::TextDisabled("%s", T("Setas / direcional: escolher    Enter / ✕ (A): confirmar    "
                                    "Esc / ○ (B): voltar    Del / △ (Y): apagar",
                                    "Arrows / D-pad: select    Enter / ✕ (A): confirm    "
                                    "Esc / ○ (B): back    Del / △ (Y): delete"));
    }
    ImGui::End();
}

// sotc: the port's loading screen while the game shows nothing (boot, shader cache, long loads):
// the title, a turning arc and "Loading…" in the launcher's language, over a dark backdrop.
void LoadingScreen() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    // sotc: opaque, so the game's last frame does not show through looking frozen.
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::Begin("##loading", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 size = viewport->WorkSize;
    const ImVec2 origin = viewport->WorkPos;
    const float unit = size.y / 1080.0f;
    const ImU32 dim = IM_COL32(150, 150, 150, 255);
    const float t = float(NowMs() % 1000000) / 1000.0f;
    // The title breathes slowly (gold, 65-100%).
    const char* title = "SHADOW OF THE COLOSSUS";
    const float title_px = 46.0f * unit;
    const ImVec2 title_size = ImGui::GetFont()->CalcTextSizeA(title_px, FLT_MAX, 0.0f, title);
    const int glow = int(255.0f * (0.825f + 0.175f * std::sin(t * 2.0f)));
    const ImU32 gold = IM_COL32(214, 184, 120, glow);
    const float title_y = origin.y + size.y * 0.40f;
    draw->AddText(ImGui::GetFont(), title_px,
                  ImVec2(origin.x + (size.x - title_size.x) * 0.5f, title_y), gold, title);
    // A turning arc under the title, with a faint full ring behind it.
    const ImVec2 centre(origin.x + size.x * 0.5f, title_y + title_px + 70.0f * unit);
    const float radius = 22.0f * unit;
    draw->AddCircle(centre, radius, IM_COL32(214, 184, 120, 50), 48, 3.0f * unit);
    draw->PathArcTo(centre, radius, t * 5.0f, t * 5.0f + 1.6f, 32);
    draw->PathStroke(IM_COL32(214, 184, 120, 255), 0, 3.5f * unit);
    // "Loading" with moving dots.
    const char* loading = SotcUi::T("Carregando", "Loading");
    char text[96];
    const int dots = int(t * 2.5f) % 4;
    std::snprintf(text, sizeof(text), "%s%.*s", loading, dots, "...");
    const float text_px = 24.0f * unit;
    const ImVec2 full = ImGui::GetFont()->CalcTextSizeA(text_px, FLT_MAX, 0.0f,
                                                        (std::string(loading) + "...").c_str());
    draw->AddText(ImGui::GetFont(), text_px,
                  ImVec2(centre.x - full.x * 0.5f, centre.y + radius + 22.0f * unit), dim, text);
    // While the shader cache is read at boot: a bar with the percentage.
    const u32 total = SotcStats::warmup_total.load(std::memory_order_relaxed);
    // sotc: without a cache to read (the first start, or a cache of another version) the shaders
    // are translated as the game asks for them, a number not known ahead: a running bar with the
    // count of this loading screen, so a slow PC does not look frozen.
    static u64 compiled_base = 0;
    static long long last_frame_ms = 0;
    const u64 compiled = SotcStats::shaders_compiled.load(std::memory_order_relaxed);
    const long long now_ms = NowMs();
    if (now_ms - last_frame_ms > 500) {
        compiled_base = compiled;
    }
    last_frame_ms = now_ms;
    if (total == 0 && compiled > compiled_base) {
        const float bar_w = size.x * 0.4f, bar_h = 10.0f * unit;
        const ImVec2 bar0(origin.x + (size.x - bar_w) * 0.5f, centre.y + radius + 70.0f * unit);
        const float span = bar_w * 0.25f;
        const float phase = std::fmod(t * 0.6f, 1.0f);
        const float x0 = bar0.x + (bar_w + span) * phase - span;
        draw->AddRectFilled(bar0, ImVec2(bar0.x + bar_w, bar0.y + bar_h), IM_COL32(60, 60, 64, 255),
                            bar_h * 0.5f);
        draw->PushClipRect(bar0, ImVec2(bar0.x + bar_w, bar0.y + bar_h), true);
        draw->AddRectFilled(ImVec2(x0, bar0.y), ImVec2(x0 + span, bar0.y + bar_h), gold,
                            bar_h * 0.5f);
        draw->PopClipRect();
        char line[192];
        std::snprintf(line, sizeof(line), SotcUi::T("Compilando shaders: %llu", "Compiling shaders: %llu"),
                      static_cast<unsigned long long>(compiled - compiled_base));
        const float line_px = 22.0f * unit;
        const ImVec2 line_size = ImGui::GetFont()->CalcTextSizeA(line_px, FLT_MAX, 0.0f, line);
        draw->AddText(ImGui::GetFont(), line_px,
                      ImVec2(origin.x + (size.x - line_size.x) * 0.5f, bar0.y + bar_h + 14.0f * unit),
                      dim, line);
    }
    if (total > 0) {
        const u32 done = std::min(SotcStats::warmup_done.load(std::memory_order_relaxed), total);
        const float frac = float(done) / float(total);
        const float bar_w = size.x * 0.4f, bar_h = 10.0f * unit;
        const ImVec2 bar0(origin.x + (size.x - bar_w) * 0.5f, centre.y + radius + 70.0f * unit);
        draw->AddRectFilled(bar0, ImVec2(bar0.x + bar_w, bar0.y + bar_h), IM_COL32(60, 60, 64, 255), bar_h * 0.5f);
        draw->AddRectFilled(bar0, ImVec2(bar0.x + bar_w * frac, bar0.y + bar_h), gold, bar_h * 0.5f);
        char line[192];
        std::snprintf(line, sizeof(line), "%s %d%%  (%u / %u)", SotcUi::T("Carregando shaders…", "Loading shaders…"),
                      int(frac * 100.0f + 0.5f), done, total);
        const float line_px = 22.0f * unit;
        const ImVec2 line_size = ImGui::GetFont()->CalcTextSizeA(line_px, FLT_MAX, 0.0f, line);
        draw->AddText(ImGui::GetFont(), line_px,
                      ImVec2(origin.x + (size.x - line_size.x) * 0.5f, bar0.y + bar_h + 14.0f * unit), dim, line);
    }
    ImGui::End();
}

void FpsCounter() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 12.0f * base_scale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad,
                                   viewport->WorkPos.y + pad),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("##fps", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    // Latest frames in order, oldest first.
    float frames[kHistory];
    for (int i = 0; i < hist_count; ++i) {
        frames[i] = frame_hist[(hist_pos - hist_count + i + kHistory) % kHistory];
    }
    const float target = TargetMs();
    float sum = 0.0f, worst = 0.0f;
    int late = 0;
    for (int i = 0; i < hist_count; ++i) {
        sum += frames[i];
        worst = std::max(worst, frames[i]);
        late += frames[i] > target * 1.1f;
    }
    float sorted[kHistory];
    std::copy(frames, frames + hist_count, sorted);
    std::sort(sorted, sorted + hist_count);
    const float p99 = hist_count ? sorted[std::min(hist_count - 1, hist_count * 99 / 100)] : 0.0f;
    const float avg_fps = sum > 0.0f ? 1000.0f * hist_count / sum : 0.0f;
    const float seconds = sum / 1000.0f;

    const ImVec4 green(0.45f, 0.9f, 0.45f, 1.0f), yellow(1.0f, 0.82f, 0.3f, 1.0f),
        red(1.0f, 0.4f, 0.35f, 1.0f), grey(0.7f, 0.7f, 0.7f, 1.0f);
    const auto rate_color = [&](float ms) {
        return ms <= target * 1.1f ? green : ms <= target * 1.6f ? yellow : red;
    };
    using SotcUi::T;
    const float now_ms = frame_ms_avg;
    ImGui::TextColored(rate_color(now_ms), "%.0f FPS", now_ms > 0.0f ? 1000.0f / now_ms : 0.0f);
    ImGui::SameLine();
    ImGui::TextColored(grey, T("%.1f ms   alvo %.0f FPS", "%.1f ms   target %.0f FPS"), now_ms,
                       1000.0f / target);

    // Frame time graph, 0 to 2.2x the target, with the target line.
    const float graph_max = target * 2.2f;
    const ImVec2 graph_size(260.0f * base_scale, 54.0f * base_scale);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, late ? yellow : green);
    ImGui::PlotLines("##frametimes", frames, hist_count, 0, nullptr, 0.0f, graph_max, graph_size);
    ImGui::PopStyleColor();
    const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    const float y = hi.y - (hi.y - lo.y) * (target / graph_max);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lo.x, y), ImVec2(hi.x, y),
                                        IM_COL32(120, 200, 255, 160), 1.0f);

    ImGui::Text(T("%.0f s: média %.1f | 1%% low %.1f | pior %.0f ms",
                  "%.0f s: avg %.1f | 1%% low %.1f | worst %.0f ms"),
                seconds, avg_fps, p99 > 0.0f ? 1000.0f / p99 : 0.0f, worst);
    ImGui::TextColored(late ? (late > 3 ? red : yellow) : green, T("Atrasados: %d", "Late: %d"),
                       late);
    ImGui::SameLine();
    ImGui::TextColored(grey,
                       T("| sessão %llu (%.1f%%), travadas >50 ms %llu",
                         "| session %llu (%.1f%%), stutters >50 ms %llu"),
                       static_cast<unsigned long long>(session_late),
                       session_frames ? 100.0 * session_late / session_frames : 0.0,
                       static_cast<unsigned long long>(session_stutters));
    if (SotcStats::kFpsLog) {
        ImGui::TextColored(grey,
                           T("CPU %.0f comandos/quadro | leituras da GPU %.0f/s (%.0f ms/s)",
                             "CPU %.0f commands/frame | GPU readbacks %.0f/s (%.0f ms/s)"),
                           SotcStats::ov_cmds_per_frame.load(), SotcStats::ov_readbacks_per_s.load(),
                           SotcStats::ov_readback_ms_per_s.load());
        const int cause = SotcStats::ov_worst_cause.load();
        ImGui::TextColored(grey,
                           T("Shaders compilados: %u (5 s), %llu na sessão",
                             "Shaders compiled: %u (5 s), %llu this session"),
                           SotcStats::ov_compiles_window.load(),
                           static_cast<unsigned long long>(SotcStats::ov_compiles_total.load()));
        if (cause >= 0) {
            ImGui::SameLine();
            ImGui::TextColored(grey, T("| pior quadro: %s %.0f ms", "| worst frame: %s %.0f ms"),
                               SotcStats::kFrameCauseNames[cause],
                               SotcStats::ov_worst_cause_ms.load());
        }
    }
    // Frame generation (vk_framegen.h): DLSS-G (vk_dlssg.cpp) or FSR 3.1 (vk_fsrfg.cpp).
    switch (Vulkan::FrameGen::Current()) {
    case Vulkan::FrameGen::Mode::Off:
        ImGui::TextColored(grey, "%s", T("Geração de quadros: desligada", "Frame generation: off"));
        break;
    case Vulkan::FrameGen::Mode::Dlss:
        if (const int fg = SotcStats::ov_fg_state.load(std::memory_order_relaxed); fg == 1) {
            const uint32_t shown = SotcStats::ov_fg_frames.load(std::memory_order_relaxed);
            const uint32_t n = shown ? shown : 2;
            ImGui::TextColored(green, T("DLSS-G x%u: ~%.0f FPS na tela", "DLSS-G x%u: ~%.0f FPS shown"),
                               n, avg_fps * n);
        } else if (fg == 2) {
            ImGui::TextColored(red, T("DLSS-G: erro (status %#x)", "DLSS-G: error (status %#x)"),
                               SotcStats::ov_fg_status.load(std::memory_order_relaxed));
        } else {
            ImGui::TextColored(grey, "%s",
                               T("DLSS-G: aguardando (sem cena 3D)", "DLSS-G: waiting (no 3D scene)"));
        }
        break;
    case Vulkan::FrameGen::Mode::Fsr:
        if (const int fg = SotcStats::ov_fsr_state.load(std::memory_order_relaxed); fg == 1) {
            ImGui::TextColored(green, T("FSR 3.1 x2: ~%.0f FPS na tela", "FSR 3.1 x2: ~%.0f FPS shown"),
                               avg_fps * 2);
        } else if (fg == 2) {
            ImGui::TextColored(red, "%s", T("FSR 3.1: erro (veja o registro)", "FSR 3.1: error (see the log)"));
        } else {
            ImGui::TextColored(grey, "%s",
                               T("FSR 3.1: aguardando (sem cena 3D)", "FSR 3.1: waiting (no 3D scene)"));
        }
        break;
    }
    if (const uint64_t vram = SotcStats::vram_used.load(std::memory_order_relaxed)) {
        ImGui::TextColored(grey, T("VRAM %.1f / %.1f GB", "VRAM %.1f / %.1f GB"),
                           vram / 1073741824.0,
                           SotcStats::vram_budget.load(std::memory_order_relaxed) / 1073741824.0);
        ImGui::SameLine();
    }
    ImGui::TextDisabled(T("F7: %s   F8: geração de quadros   F10: esconder",
                          "F7: %s   F8: frame generation   F10: hide"),
                        SotcFpsCap::uncapped ? T("FPS livres", "FPS uncapped")
                                             : T("travado em 30", "capped at 30"));
    ImGui::End();
}

void Toast() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                                   viewport->WorkPos.y + 24.0f * base_scale),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin("##toast", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextUnformatted(toast_text.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
}

// sotc: Chinese, Japanese and Korean texts: a system font of the UI language (%WINDIR%\Fonts),
// merged into the embedded one for the glyphs it lacks. Only the glyphs drawn take atlas space.
void AddLanguageFont(ImFontAtlas& atlas, float size) {
    using SotcUi::Lang;
    struct LanguageFont {
        Lang lang;
        const char* name;
        std::array<const char*, 3> files; // in order of preference
        const ImWchar* (ImFontAtlas::*ranges)();
    };
    static constexpr LanguageFont fonts[] = {
        {Lang::Zh, "zh", {"msyh.ttc", "msyh.ttf"}, &ImFontAtlas::GetGlyphRangesChineseSimplifiedCommon},
        {Lang::Ja, "ja", {"YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"}, &ImFontAtlas::GetGlyphRangesJapanese},
        {Lang::Ko, "ko", {"malgun.ttf"}, &ImFontAtlas::GetGlyphRangesKorean},
    };
    const auto font = std::ranges::find(fonts, SotcUi::Current(), &LanguageFont::lang);
    if (font == std::end(fonts)) {
        return;
    }
#ifdef _WIN32
    const wchar_t* windir = _wgetenv(L"WINDIR");
#else
    const char* windir = std::getenv("WINDIR");
#endif
    if (windir && windir[0]) {
        const std::filesystem::path dir = std::filesystem::path{windir} / "Fonts";
        ImFontConfig config;
        config.MergeMode = true;
        config.Flags |= ImFontFlags_NoLoadError;
        for (const char* file : font->files) {
            std::error_code error;
            if (!file || !std::filesystem::is_regular_file(dir / file, error)) {
                continue;
            }
            const std::u8string path = (dir / file).u8string();
            if (atlas.AddFontFromFileTTF(reinterpret_cast<const char*>(path.c_str()), size, &config,
                                         (atlas.*(font->ranges))())) {
                std::printf("Overlay: %s texts with %s\n", font->name, file);
                return;
            }
        }
    }
    std::printf("Overlay: no %s font (%s) in %%WINDIR%%\\Fonts: its characters show as '?'\n",
                font->name, font->files[0]);
}

} // namespace

void Init(const Vulkan::Instance& instance, vk::Format format, u32 image_count) {
    s_instance = &instance;
    std::scoped_lock lock{imgui_mutex};
    if (initialized) {
        return;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // window positions are not kept
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.BackendPlatformName = "bbport";

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;

    // sotc: the embedded font has Latin, Cyrillic and the symbols of the texts. ImGui 1.92 with
    // this backend rasterizes glyphs on demand; the ranges only matter where they are preloaded.
    static constexpr ImWchar kTextRanges[] = {
        0x0020, 0x00FF, // Basic Latin, Latin-1
        0x0400, 0x052F, // Cyrillic and its supplement
        0x2000, 0x206F, // punctuation (dashes, ellipsis)
        0x2190, 0x21FF, // arrows
        0x25A0, 0x25FF, // geometric shapes (○ △)
        0x2715, 0x2715, // ✕
        0,
    };
    constexpr float kFontSize = 18.0f;
    ImFontConfig font_config;
    font_config.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(bb_font_ttf),
                                   int(bb_font_ttf_end - bb_font_ttf), kFontSize, &font_config,
                                   kTextRanges);
    AddLanguageFont(*io.Fonts, kFontSize);

    const vk::Instance vk_instance = instance.GetInstance();
    ImGui_ImplVulkan_LoadFunctions(
        instance.ApiVersion(),
        [](const char* name, void* user) {
            return VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr(
                *static_cast<const vk::Instance*>(user), name);
        },
        const_cast<vk::Instance*>(&vk_instance));

    const VkFormat color_format = static_cast<VkFormat>(format);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = instance.ApiVersion();
    info.Instance = vk_instance;
    info.PhysicalDevice = instance.GetPhysicalDevice();
    info.Device = instance.GetDevice();
    info.QueueFamily = instance.GetGraphicsQueueFamilyIndex();
    info.Queue = instance.GetGraphicsQueue();
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(image_count, 2u);
    info.ImageCount = std::max(image_count, 2u);
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        std::printf("Overlay: ImGui Vulkan backend init failed\n");
        ImGui::DestroyContext();
        return;
    }
    initialized = true;
    std::printf("Overlay: menu ready (Insert or L3+R3)\n");
}

void UpdateTextInput(SDL_Window* window) {
    bool want = false;
    {
        std::scoped_lock lock{imgui_mutex};
        want = initialized && menu_open && ImGui::GetIO().WantTextInput;
    }
    if (want != SDL_TextInputActive(window)) {
        if (want) {
            SDL_StartTextInput(window);
        } else {
            SDL_StopTextInput(window);
        }
    }
}

bool HandleEvent(const SDL_Event& event) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    // sotc: F11 (fullscreen, window.cpp) also works while the menu is open.
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
        event.key.key == SDLK_F11) {
        return false;
    }
    // sotc: F7 caps the game at 30 FPS or uncaps it (sotc_fps_cap.h).
    // The new state is shown at the next frame (Render).
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F7 && !event.key.repeat) {
        SotcFpsCap::Toggle();
        return true;
    }
    // sotc: F8 switches the frame generation mode (off, DLSS-G, FSR 3.1; vk_framegen.h).
    // The new mode is shown at the next frame (Render).
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F8 && !event.key.repeat) {
        const Vulkan::FrameGen::Mode mode = Vulkan::FrameGen::Cycle();
        if (mode == Vulkan::FrameGen::Mode::Off &&
            !Vulkan::FrameGen::Available(Vulkan::FrameGen::Mode::Fsr) &&
            !Vulkan::FrameGen::Available(Vulkan::FrameGen::Mode::Dlss)) {
            toast_text = SotcUi::T("Geração de quadros: indisponível nesta placa", "Frame generation: not available on this GPU");
            toast_until_ms.store(NowMs() + 2500, std::memory_order_relaxed);
        }
        return true;
    }
    // sotc: F10 shows or hides the performance overlay.
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F10 && !event.key.repeat) {
        const bool on = !OverlayOn();
        BbSettings::Get().show_fps = false; // the ini setting only gives the initial state
        overlay_on.store(on, std::memory_order_relaxed);
        return true;
    }
    if (list_active && !menu_open && list_result == -1) {
        const int count = static_cast<int>(list_items.size());
        const auto move = [&](int delta) {
            list_confirm = -1;
            if (count) {
                list_selected = (list_selected + delta + count) % count;
                list_scroll = true;
            }
        };
        switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            switch (event.key.key) {
            case SDLK_UP:
            case SDLK_W:
                move(-1);
                break;
            case SDLK_DOWN:
            case SDLK_S:
                move(1);
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (!event.key.repeat && list_confirm >= 0) {
                    list_result = -100 - list_confirm; // delete it (PollSaveList)
                    list_confirm = -1;
                } else if (!event.key.repeat && count) {
                    list_result = list_selected;
                }
                break;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                if (!event.key.repeat) {
                    if (list_confirm >= 0) {
                        list_confirm = -1;
                    } else {
                        list_result = -2;
                    }
                }
                break;
            case SDLK_DELETE:
                if (!event.key.repeat && list_selected >= list_first_deletable &&
                    list_selected < count) {
                    list_confirm = list_selected;
                }
                break;
            default:
                break;
            }
            return true;
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            switch (event.gbutton.button) {
            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                move(-1);
                break;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                move(1);
                break;
            case SDL_GAMEPAD_BUTTON_SOUTH:
                if (list_confirm >= 0) {
                    list_result = -100 - list_confirm;
                    list_confirm = -1;
                } else if (count) {
                    list_result = list_selected;
                }
                break;
            case SDL_GAMEPAD_BUTTON_EAST:
                if (list_confirm >= 0) {
                    list_confirm = -1;
                } else {
                    list_result = -2;
                }
                break;
            case SDL_GAMEPAD_BUTTON_NORTH:
                if (list_selected >= list_first_deletable && list_selected < count) {
                    list_confirm = list_selected;
                }
                break;
            default:
                break;
            }
            return true;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            return true;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                const bool down = event.gaxis.value > 20000, up = event.gaxis.value < -20000;
                if (down && !list_stick_down) {
                    move(1);
                }
                if (up && !list_stick_up) {
                    move(-1);
                }
                list_stick_down = down;
                list_stick_up = up;
            }
            return true;
        case SDL_EVENT_MOUSE_MOTION: {
            const float density = PixelDensity(event.motion.windowID);
            io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
            return true;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                io.AddMouseButtonEvent(0, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
            }
            return true;
        case SDL_EVENT_MOUSE_WHEEL:
            io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
            return true;
        default:
            break;
        }
    }
    const bool is_open = menu_open;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        const bool down = event.type == SDL_EVENT_KEY_DOWN;
        const bool toggle_key = (event.key.key == SDLK_INSERT || event.key.key == SDLK_F1);
        if (down && !event.key.repeat && MenuAllowed() &&
            (toggle_key || (is_open && event.key.key == SDLK_ESCAPE))) {
            SetOpen(toggle_key ? !is_open : false);
            return true;
        }
        if (!is_open) {
            return false;
        }
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0);
        if (const ImGuiKey key = KeyFromSdl(event.key.key); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        const u8 button = event.gbutton.button;
        if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) {
            l3_down = down;
        } else if (button == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            r3_down = down;
        }
        if (down && l3_down && r3_down && MenuAllowed()) {
            SetOpen(!is_open);
            return true;
        }
        if (!is_open) {
            return false;
        }
        if (const ImGuiKey key = KeyFromGamepad(button); key != ImGuiKey_None) {
            io.AddKeyEvent(key, down);
        }
        return true;
    }
    case SDL_EVENT_TEXT_INPUT: {
        // Typed characters (Ctrl+click on a slider, a text field): key events alone erase but
        // do not type. SDL sends them while text input is on (UpdateTextInput).
        if (!is_open) {
            return false;
        }
        io.AddInputCharactersUTF8(event.text.text);
        return true;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (!is_open) {
            return false;
        }
        const float density = PixelDensity(event.motion.windowID);
        io.AddMousePosEvent(event.motion.x * density, event.motion.y * density);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (!is_open) {
            return false;
        }
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT  ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                      : -1;
        if (button >= 0) {
            io.AddMouseButtonEvent(button, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        if (!is_open) {
            return false;
        }
        io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
        return true;
    default:
        return false;
    }
}

// sotc: the port's loading screen over the game while the pad skips the opening movie
// (runtime_pad.c skip_movie): the movie is neither seen nor heard.
static std::atomic<bool> intro_cover{false};

bool LoadingShown() {
    if (intro_cover.load(std::memory_order_relaxed)) {
        return true;
    }
    // sotc: a second without a game flip. Read from the presenter and the video out threads: the
    // races only move the moment by a frame.
    static std::atomic<u64> seen{~u64{0}};
    static std::atomic<long long> since{0};
    const u64 flips = SotcStats::flip_count.load(std::memory_order_relaxed);
    const long long now = NowMs();
    if (seen.exchange(flips, std::memory_order_relaxed) != flips) {
        since.store(now, std::memory_order_relaxed);
        return false;
    }
    return now - since.load(std::memory_order_relaxed) >= 1000;
}

void SetIntroCover(bool on) {
    intro_cover.store(on, std::memory_order_relaxed);
}

bool Visible() {
    return initialized && (menu_open || text_entry_active || list_active || OverlayOn() ||
                           ToastShown() || LoadingShown());
}

bool CapturesInput() {
    return menu_open || text_entry_active || list_active;
}

bool OpenSaveList(const std::string& title, const std::vector<std::string>& items, int focus,
                  int first_deletable) {
    std::scoped_lock lock{imgui_mutex};
    if (!initialized) {
        return false; // nothing could show it
    }
    list_title = title;
    list_items = items;
    list_icons = std::move(pending_icons); // sotc: the pictures sent just before (or none)
    pending_icons.clear();
    list_selected = items.empty() ? 0 : std::clamp(focus, 0, static_cast<int>(items.size()) - 1);
    list_result = -1;
    list_confirm = -1;
    list_first_deletable = first_deletable < 0 ? static_cast<int>(items.size()) : first_deletable;
    list_scroll = true;
    list_active = true;
    ImGui::GetIO().MouseDrawCursor = true;
    return true;
}

void SetSaveListIcons(std::vector<std::string> paths) {
    std::scoped_lock lock{imgui_mutex};
    pending_icons = std::move(paths);
}

int PollSaveList() {
    std::scoped_lock lock{imgui_mutex};
    if (!list_active || list_result == -1) {
        return -1;
    }
    list_active = false;
    if (initialized) {
        ImGui::GetIO().MouseDrawCursor = menu_open.load();
    }
    return list_result;
}

void CloseSaveList() {
    std::scoped_lock lock{imgui_mutex};
    list_active = false;
    if (initialized) {
        ImGui::GetIO().MouseDrawCursor = menu_open.load();
    }
}

void SetTextEntry(bool active, const std::string& prompt, const std::string& text) {
    std::scoped_lock lock{imgui_mutex};
    text_entry_active = active;
    text_entry_prompt = prompt;
    text_entry_text = text;
}

void Render(vk::CommandBuffer cmdbuf, vk::ImageView view, vk::Extent2D extent,
            bool game_frame) {
    // Present interval for the FPS readout (measured also while nothing is drawn), game frames
    // only: a generated frame (FSR 3.1) is drawn half a frame later.
    const auto now = std::chrono::steady_clock::now();
    static auto last_draw = now;
    const float draw_ms = std::chrono::duration<float, std::milli>(now - last_draw).count();
    last_draw = now;
    if (game_frame) {
        const float ms = std::chrono::duration<float, std::milli>(now - last_present).count();
        last_present = now;
        if (ms > 0.0f && ms < 1000.0f) {
            frame_ms_avg = frame_ms_avg == 0.0f ? ms : frame_ms_avg * 0.95f + ms * 0.05f;
        }
        if (ms > 0.0f && ms < 5000.0f) {
            RecordFrame(ms);
        }
    }
    const float ms = draw_ms;
    // The frame generation mode changed (F8): shown for 2.5 s.
    static Vulkan::FrameGen::Mode shown_mode = Vulkan::FrameGen::Current();
    if (const Vulkan::FrameGen::Mode mode = Vulkan::FrameGen::Current(); mode != shown_mode) {
        shown_mode = mode;
        char text[128];
        std::snprintf(text, sizeof(text), SotcUi::T("Geração de quadros: %s", "Frame generation: %s"),
                      Vulkan::FrameGen::Name(mode));
        std::scoped_lock lock{imgui_mutex};
        toast_text = text;
        toast_until_ms.store(NowMs() + 2500, std::memory_order_relaxed);
    }
    // The 30 FPS cap was switched (F7).
    static bool shown_uncapped = SotcFpsCap::uncapped;
    if (const bool uncapped = SotcFpsCap::uncapped; uncapped != shown_uncapped) {
        shown_uncapped = uncapped;
        std::scoped_lock lock{imgui_mutex};
        toast_text = uncapped ? SotcUi::T("FPS livres", "FPS uncapped")
                              : SotcUi::T("FPS travados em 30", "FPS capped at 30");
        toast_until_ms.store(NowMs() + 2500, std::memory_order_relaxed);
    }
    if (!Visible()) {
        return;
    }
    std::scoped_lock lock{imgui_mutex};
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    io.DeltaTime = ms > 0.0f && ms < 1000.0f ? ms / 1000.0f : 1.0f / 60.0f;
    // UI scale follows the display height (1080p = 1).
    const float scale = std::max(float(extent.height) / 1080.0f, 0.75f);
    if (std::abs(scale - base_scale) > 0.01f) {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(scale / base_scale);
        style.FontScaleMain = scale;
        base_scale = scale;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    const bool loading = LoadingShown() && !list_active && !text_entry_active;
    static bool was_loading = false;
    static long long loading_since = 0;
    if (loading != was_loading) { // sotc: one log line when it appears and when it goes away
        was_loading = loading;
        if (loading) {
            loading_since = NowMs();
            std::fprintf(stderr, "SOTC loading screen: shown\n");
        } else {
            std::fprintf(stderr, "SOTC loading screen: hidden after %.1f s\n",
                         double(NowMs() - loading_since) / 1000.0);
        }
    }
    if (loading) {
        LoadingScreen();
    }
    if (menu_open) {
        Menu();
    }
    if (OverlayOn() && !menu_open) {
        FpsCounter();
    }
    if (ToastShown()) {
        Toast();
    }
    if (text_entry_active) {
        TextEntryBox();
    }
    if (list_active && !menu_open) {
        SaveListBox();
    }
    ImGui::Render();

    const vk::RenderingAttachmentInfo attachment{
        .imageView = view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    cmdbuf.beginRendering(vk::RenderingInfo{
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &attachment,
    });
    {
        // Font atlas uploads submit to the graphics queue themselves.
        std::scoped_lock submit_lock{Vulkan::Scheduler::submit_mutex};
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdbuf);
    }
    cmdbuf.endRendering();
}

} // namespace BbOverlay
