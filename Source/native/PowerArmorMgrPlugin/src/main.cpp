#include <cstdio>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cctype>
#include <utility>
#include <algorithm>
#include <random>
#include <array>
#include <cmath>
#include <thread>

#include "RE/Fallout.h"
#include "F4SE/F4SE.h"
#include "F4SE/API.h"

#include <windows.h>
#include "PrismaUI_F4_API.h"

#define POWERARMORMGR_EXPORT extern "C" [[maybe_unused]] __declspec(dllexport)

namespace fs = std::filesystem;

POWERARMORMGR_EXPORT F4SE::PluginVersionData F4SEPlugin_Version = []() noexcept {
    F4SE::PluginVersionData v{};
    v.PluginVersion({ 1, 0, 0, 0 });
    v.PluginName("PowerArmorMgr");
    v.AuthorName("PowerArmorMgr Author");
    v.UsesAddressLibrary(true);
    v.UsesAddressLibraryNG(true);
    v.UsesSigScanning(false);
    v.IsLayoutDependent(true);
    v.IsLayoutDependentNG(true);
    v.HasNoStructUse(false);
    v.CompatibleVersions({
        F4SE::RUNTIME_1_10_162, F4SE::RUNTIME_1_10_163,
        F4SE::RUNTIME_1_10_980, F4SE::RUNTIME_1_10_984,
        F4SE::RUNTIME_1_11_137, F4SE::RUNTIME_1_11_159,
        F4SE::RUNTIME_1_11_169, F4SE::RUNTIME_1_11_191,
        F4SE::RUNTIME_1_11_221
    });
    return v;
}();

constexpr int MAX_SLOTS = 50;
constexpr const char* DATA_DIR = "F4SE/Plugins/PowerArmorManager";
constexpr const char* MENU_HTML = "PowerArmorMgr/menu.html";
constexpr const char* RESULT_HTML = "PowerArmorMgr/result.html";
constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) |
           (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) |
           (static_cast<uint32_t>(d) << 24);
}

static std::string ReadUiLayoutJson();
constexpr uint32_t SERIAL_UID = FourCC('P', 'A', 'M', 'S');
constexpr uint32_t SERIAL_RECORD_SLOTS = FourCC('S', 'L', 'O', 'T');
constexpr uint32_t SERIAL_VERSION = 1;

namespace json {
    inline std::string esc(std::string_view s) {
        std::string o; for (char c : s) {
            if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\"; else o += c;
        } return o;
    }
    inline std::string getStr(std::string_view j, std::string_view k) {
        auto s = "\"" + std::string(k) + "\"";
        auto p = j.find(s);
        if (p == std::string_view::npos) return {};
        p = j.find(':', p + s.size());
        if (p == std::string_view::npos) return {};
        ++p;
        while (p < j.size() && std::isspace(static_cast<unsigned char>(j[p]))) ++p;
        if (p >= j.size() || j[p] != '"') return {};
        ++p;
        auto e = j.find('\"', p); if (e == std::string_view::npos) return {};
        return std::string(j.substr(p, e - p));
    }
    inline int getInt(std::string_view j, std::string_view k, int d = 0) {
        auto s = "\"" + std::string(k) + "\"";
        auto p = j.find(s);
        if (p == std::string_view::npos) return d;
        p = j.find(':', p + s.size());
        if (p == std::string_view::npos) return d;
        ++p;
        try { return std::stoi(std::string(j.substr(p))); } catch (...) { return d; }
    }
}

static std::mutex g_mx;
static int g_curSlot = 1, g_lastRandSlot = 0, g_lastEquippedSlot = 0;
static bool g_eqBusy = false, g_randBusy = false;
static float g_eqLastT = 0, g_randLastT = 0;
static float g_menuOpenT = 0, g_menuActionT = 0;
static int g_menuAct = 0, g_menuActSlot = 0;
static PRISMA_UI_API::IVPrismaUI4* g_prisma = nullptr;
static PrismaView g_view = 0;
static PrismaView g_resultView = 0;
static bool g_menuOpen = false;
static bool g_viewReady = false;
static bool g_resultViewReady = false;
static bool g_resultOpen = false;
static bool g_hiddenBehindOverlay = false;
static bool g_timePaused = false;
static float g_previousTimeMult = 1.0F;
static bool g_overlayMonitorStarted = false;
static ULONGLONG g_lastUiWakeMs = 0;
static std::string g_pendingMenuStateScript;
static std::string g_pendingResultScript;
static bool g_cacheLoaded = false;
static RE::ObjectRefHandle g_menuTarget;
static RE::BSTSmartPointer<RE::BSInputEnableLayer> g_inputLayer;
struct OE { uint32_t fid; std::string name; };
struct SlotInfo {
    int slot = 0;
    int gender = -1;
    int count = 0;
};
struct SlotDetail {
    std::string summary;
    std::vector<std::string> itemNames;
};
struct SavedSlot {
    int gender = -1;
    uint32_t frameID = 0;
    std::string frameName;
    std::vector<OE> items;
};
struct MItems { std::vector<uint32_t> m, d; bool dr = false; };
static std::unordered_map<uint32_t, MItems> g_mi;
static std::vector<SlotInfo> g_index;
static std::unordered_map<int, SlotInfo> g_indexBySlot;
static std::unordered_map<int, SlotDetail> g_slotDetails;
static std::unordered_map<int, std::vector<int>> g_randomBags;
struct SaveDraft { int gender = -1; std::vector<OE> items; };
static std::unordered_map<int, SaveDraft> g_saveDrafts;
static std::unordered_map<int, SavedSlot> g_savedSlots;
static std::unordered_map<int, RE::ObjectRefHandle> g_powerArmorRefs;
static std::unordered_map<int, RE::TESObjectREFR*> g_powerArmorRawRefs;
static bool g_inputSinkRegistered = false;

static void InvokeMenuScript(const char* script) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_menuOpen || g_hiddenBehindOverlay || !g_prisma || !g_prisma->IsValid(g_view) || !g_viewReady || !script) return;
    g_prisma->Invoke(g_view, script);
}

static void InvokeResultScript(const char* script) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_resultOpen || !g_prisma || !g_prisma->IsValid(g_resultView) || !g_resultViewReady || !script) return;
    g_prisma->Invoke(g_resultView, script);
}

static void InvokeGamepadMenuScript(const char* script) {
    if (!script) return;
    std::string combined = "if(window.omUseGamepad)window.omUseGamepad();";
    combined += script;
    InvokeMenuScript(combined.c_str());
}

static void InvokeInputMenuScript(const RE::InputEvent* event, const char* script) {
    if (!event || !script) return;
    if (event->device == RE::INPUT_DEVICE::kGamepad) {
        InvokeGamepadMenuScript(script);
        return;
    }
    std::string combined = "if(window.omUseKeyboard)window.omUseKeyboard();";
    combined += script;
    InvokeMenuScript(combined.c_str());
}

static void StopInputEvent(const RE::InputEvent* event) {
    if (!event) return;
    const_cast<RE::InputEvent*>(event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
}

static void SetGameplayInputBlocked(bool block) {
    auto* manager = RE::BSInputEnableManager::GetSingleton();
    if (!manager) return;

    if (block) {
        if (!g_inputLayer) {
            manager->AllocateNewLayer(g_inputLayer, "PowerArmorMgrUI");
        }
        if (g_inputLayer) {
            manager->EnableUserEvent(g_inputLayer->layerID, RE::UserEvents::USER_EVENT_FLAG::kAll, false, RE::UserEvents::SENDER_ID::kMenu);
        }
        return;
    }

    if (g_inputLayer) {
        manager->EnableUserEvent(g_inputLayer->layerID, RE::UserEvents::USER_EVENT_FLAG::kAll, true, RE::UserEvents::SENDER_ID::kMenu);
        g_inputLayer.reset();
    }
}

static void SetMenuTimePaused(bool enabled) {
    auto* timer = RE::BSTimer::GetSingleton();
    if (!timer) return;

    if (enabled) {
        if (!g_timePaused) {
            g_previousTimeMult = RE::BSTimer::QGlobalTimeMultiplier();
            g_timePaused = true;
        }
        timer->SetGlobalTimeMultiplier(0.02F, true);
        return;
    }

    if (g_timePaused) {
        timer->SetGlobalTimeMultiplier(g_previousTimeMult <= 0.0F ? 1.0F : g_previousTimeMult, true);
        g_timePaused = false;
    }
}

static bool BlockingNativeMenuOpen() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui || ui->menuMode != 0) return true;

    constexpr std::array<const char*, 10> names = {
        "Console", "PauseMenu", "PipboyMenu", "MessageBoxMenu", "DialogueMenu",
        "ContainerMenu", "BarterMenu", "TerminalMenu", "LoadingMenu", "MainMenu"
    };
    for (const auto* name : names) {
        if (ui->GetMenuOpen(RE::BSFixedString(name))) return true;
    }
    return false;
}

struct OtherPrismaViewState {
    PrismaView self = 0;
    bool active = false;
};

static bool IsHudPrismaPath(const char* path) {
    if (!path) return false;
    std::string lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lower.find("hud") != std::string::npos;
}

static void DetectOtherPrismaView(PrismaView id, const char* htmlPath, void* userdata) {
    auto* state = static_cast<OtherPrismaViewState*>(userdata);
    if (!state || !g_prisma || id == 0 || id == state->self || !g_prisma->IsValid(id)) return;
    if (IsHudPrismaPath(htmlPath)) return;
    if (!g_prisma->IsHidden(id)) state->active = true;
}

static bool OtherPrismaViewOpen() {
    if (!g_prisma) {
        g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>();
    }
    if (!g_prisma) return false;

    OtherPrismaViewState state{ g_view, false };
    g_prisma->EnumerateViews(DetectOtherPrismaView, &state);
    return state.active;
}

static int MenuOpenBlockReason() {
    if (BlockingNativeMenuOpen()) return 1;
    if (OtherPrismaViewOpen()) return 2;
    return 0;
}

static void HideMenuForOverlay() {
    if (!g_menuOpen || g_hiddenBehindOverlay || !g_prisma || !g_prisma->IsValid(g_view)) return;
    g_hiddenBehindOverlay = true;
    g_prisma->Unfocus(g_view);
    g_prisma->Hide(g_view);
    SetGameplayInputBlocked(false);
    SetMenuTimePaused(false);
}

static void RestoreMenuAfterOverlay() {
    if (!g_menuOpen || !g_hiddenBehindOverlay || !g_prisma || !g_prisma->IsValid(g_view)) return;
    g_hiddenBehindOverlay = false;
    SetGameplayInputBlocked(true);
    SetMenuTimePaused(true);
    g_prisma->Show(g_view);
    g_prisma->Focus(g_view, false);
}

static void UpdateOverlayCoverState() {
    if (!g_menuOpen) return;
    if (MenuOpenBlockReason() != 0) {
        HideMenuForOverlay();
        return;
    }

    RestoreMenuAfterOverlay();
    if (g_hiddenBehindOverlay || !g_prisma || !g_prisma->IsValid(g_view)) return;
    const auto now = GetTickCount64();
    if (now - g_lastUiWakeMs < 300) return;
    g_lastUiWakeMs = now;
    if (g_prisma->IsHidden(g_view)) g_prisma->Show(g_view);
    if (!g_prisma->HasFocus(g_view)) g_prisma->Focus(g_view, false);
}

static void StartOverlayMonitor() {
    if (g_overlayMonitorStarted) return;
    g_overlayMonitorStarted = true;
    std::thread([] {
        while (true) {
            Sleep(80);
            if (!g_menuOpen) continue;
            if (auto* tasks = F4SE::GetTaskInterface()) {
                tasks->AddTask([] { UpdateOverlayCoverState(); });
            }
        }
    }).detach();
}

static bool IsPowerArmorMenuInput(RE::BS_BUTTON_CODE code) {
    switch (code) {
        case RE::BS_BUTTON_CODE::kUp:
        case RE::BS_BUTTON_CODE::kW:
        case RE::BS_BUTTON_CODE::kDPAD_Up:
        case RE::BS_BUTTON_CODE::kDown:
        case RE::BS_BUTTON_CODE::kS:
        case RE::BS_BUTTON_CODE::kDPAD_Down:
        case RE::BS_BUTTON_CODE::kLeft:
        case RE::BS_BUTTON_CODE::kA:
        case RE::BS_BUTTON_CODE::kDPAD_Left:
        case RE::BS_BUTTON_CODE::kRight:
        case RE::BS_BUTTON_CODE::kD:
        case RE::BS_BUTTON_CODE::kDPAD_Right:
        case RE::BS_BUTTON_CODE::kEnter:
        case RE::BS_BUTTON_CODE::kSpace:
        case RE::BS_BUTTON_CODE::kE:
        case RE::BS_BUTTON_CODE::kAButton:
        case RE::BS_BUTTON_CODE::kTab:
        case RE::BS_BUTTON_CODE::kBackspace:
        case RE::BS_BUTTON_CODE::kBButton:
        case RE::BS_BUTTON_CODE::kBack:
        case RE::BS_BUTTON_CODE::kEscape:
        case RE::BS_BUTTON_CODE::kStart:
        case RE::BS_BUTTON_CODE::kLShoulder:
        case RE::BS_BUTTON_CODE::kRShoulder:
            return true;
        default:
            return false;
    }
}

static bool IsPowerArmorResultInput(RE::BS_BUTTON_CODE code) {
    switch (code) {
        case RE::BS_BUTTON_CODE::kEscape:
        case RE::BS_BUTTON_CODE::kStart:
        case RE::BS_BUTTON_CODE::kEnter:
        case RE::BS_BUTTON_CODE::kSpace:
        case RE::BS_BUTTON_CODE::kE:
        case RE::BS_BUTTON_CODE::kAButton:
        case RE::BS_BUTTON_CODE::kTab:
        case RE::BS_BUTTON_CODE::kBackspace:
        case RE::BS_BUTTON_CODE::kBButton:
        case RE::BS_BUTTON_CODE::kBack:
            return true;
        default:
            return false;
    }
}

class PowerArmorMgrInputHandler : public RE::BSInputEventUser {
public:
    static PowerArmorMgrInputHandler* GetSingleton() {
        static PowerArmorMgrInputHandler instance;
        return &instance;
    }

    bool ShouldHandleEvent(const RE::InputEvent* event) override {
        if (!event || g_hiddenBehindOverlay) return false;
        if (g_resultOpen) {
            if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) return false;
            const auto* button = static_cast<const RE::ButtonEvent*>(event);
            return IsPowerArmorResultInput(button->GetBSButtonCode());
        }
        if (!g_menuOpen) return false;
        if (event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick) {
            return event->device == RE::INPUT_DEVICE::kGamepad;
        }
        if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) return false;
        const auto* button = static_cast<const RE::ButtonEvent*>(event);
        return IsPowerArmorMenuInput(button->GetBSButtonCode());
    }

    void OnThumbstickEvent(const RE::ThumbstickEvent* event) override {
        if (!event || !g_menuOpen || g_hiddenBehindOverlay ||
            event->device != RE::INPUT_DEVICE::kGamepad) return;
        StopInputEvent(event);

        constexpr float kNeutralThreshold = 0.25F;
        constexpr float kDirectionThreshold = 0.45F;
        constexpr ULONGLONG kInitialRepeatDelayMs = 360;
        constexpr ULONGLONG kRepeatIntervalMs = 160;
        const auto now = GetTickCount64();
        const float x = event->xValue;
        const float y = event->yValue;
        const float absX = std::abs(x);
        const float absY = std::abs(y);

        if (absX < kNeutralThreshold && absY < kNeutralThreshold) {
            stickState_ = StickState::kNeutral;
            stickDirection_ = 0;
            stickStartedMs_ = 0;
            lastThumbRepeatMs_ = 0;
            return;
        }

        if (event->QIDCode() == RE::ThumbstickEvent::kRight) {
            if (now - lastThumbScrollMs_ < 60) return;
            if (absY >= kDirectionThreshold) {
                InvokeInputMenuScript(event, y > 0.0F ?
                    "if(window.omScrollItems)window.omScrollItems(-22);" :
                    "if(window.omScrollItems)window.omScrollItems(22);");
                lastThumbScrollMs_ = now;
            }
            return;
        }

        if (event->QIDCode() != RE::ThumbstickEvent::kLeft) return;

        const bool vertical = absY >= absX && absY >= kDirectionThreshold;
        const bool horizontal = absX > absY && absX >= kDirectionThreshold;
        if (!vertical && !horizontal) return;

        const int direction = vertical ? (y > 0.0F ? -1 : 1) : (x < 0.0F ? -1 : 1);
        const StickState nextState = vertical ? StickState::kVerticalActive : StickState::kHorizontalActive;
        const bool sameInput = stickState_ == nextState && stickDirection_ == direction;
        if (sameInput && now - stickStartedMs_ < kInitialRepeatDelayMs) return;
        if (sameInput && now - lastThumbRepeatMs_ < kRepeatIntervalMs) return;

        InvokeInputMenuScript(event, vertical ?
            (direction < 0 ?
                "if(window.omMoveFocus)window.omMoveFocus(-1);" :
                "if(window.omMoveFocus)window.omMoveFocus(1);") :
            (direction < 0 ?
                "if(window.omSlotPage)window.omSlotPage(-1);" :
                "if(window.omSlotPage)window.omSlotPage(1);"));

        if (!sameInput) stickStartedMs_ = now;
        stickState_ = nextState;
        stickDirection_ = direction;
        lastThumbRepeatMs_ = now;
    }

    void OnButtonEvent(const RE::ButtonEvent* event) override {
        if (!event || !event->QJustPressed() || g_hiddenBehindOverlay) return;
        const auto code = event->GetBSButtonCode();
        if (g_resultOpen) {
            if (!IsPowerArmorResultInput(code)) return;
            StopInputEvent(event);
            InvokeResultScript("if(window.omCloseResult)window.omCloseResult();");
            return;
        }
        if (!g_menuOpen) return;
        StopInputEvent(event);

        switch (code) {
            case RE::BS_BUTTON_CODE::kEscape:
            case RE::BS_BUTTON_CODE::kStart:
                InvokeInputMenuScript(event, "if(window.omBackOrClose)window.omBackOrClose(true);");
                break;
            case RE::BS_BUTTON_CODE::kUp:
            case RE::BS_BUTTON_CODE::kW:
            case RE::BS_BUTTON_CODE::kDPAD_Up:
                InvokeInputMenuScript(event, "if(window.omMoveFocus)window.omMoveFocus(-1);");
                break;
            case RE::BS_BUTTON_CODE::kDown:
            case RE::BS_BUTTON_CODE::kS:
            case RE::BS_BUTTON_CODE::kDPAD_Down:
                InvokeInputMenuScript(event, "if(window.omMoveFocus)window.omMoveFocus(1);");
                break;
            case RE::BS_BUTTON_CODE::kLeft:
            case RE::BS_BUTTON_CODE::kA:
            case RE::BS_BUTTON_CODE::kDPAD_Left:
                InvokeInputMenuScript(event, "if(window.omSlotPage)window.omSlotPage(-1);");
                break;
            case RE::BS_BUTTON_CODE::kRight:
            case RE::BS_BUTTON_CODE::kD:
            case RE::BS_BUTTON_CODE::kDPAD_Right:
                InvokeInputMenuScript(event, "if(window.omSlotPage)window.omSlotPage(1);");
                break;
            case RE::BS_BUTTON_CODE::kEnter:
            case RE::BS_BUTTON_CODE::kSpace:
            case RE::BS_BUTTON_CODE::kE:
            case RE::BS_BUTTON_CODE::kAButton:
                InvokeInputMenuScript(event, "if(window.omActivateFocused)window.omActivateFocused();");
                break;
            case RE::BS_BUTTON_CODE::kTab:
            case RE::BS_BUTTON_CODE::kBackspace:
            case RE::BS_BUTTON_CODE::kBButton:
            case RE::BS_BUTTON_CODE::kBack:
                InvokeInputMenuScript(event, "if(window.omBackOrClose)window.omBackOrClose(true);");
                break;
            case RE::BS_BUTTON_CODE::kLShoulder:
                InvokeInputMenuScript(event, "if(window.omGamepadShoulder)window.omGamepadShoulder(-1);");
                break;
            case RE::BS_BUTTON_CODE::kRShoulder:
                InvokeInputMenuScript(event, "if(window.omGamepadShoulder)window.omGamepadShoulder(1);");
                break;
            default:
                break;
        }
    }

private:
    enum class StickState {
        kNeutral,
        kVerticalActive,
        kHorizontalActive
    };

    StickState stickState_ = StickState::kNeutral;
    int stickDirection_ = 0;
    ULONGLONG stickStartedMs_ = 0;
    ULONGLONG lastThumbRepeatMs_ = 0;
    ULONGLONG lastThumbScrollMs_ = 0;
};

static void RegisterInputSink() {
    if (g_inputSinkRegistered) return;
    if (auto* menuControls = RE::MenuControls::GetSingleton()) {
        menuControls->handlers.insert(menuControls->handlers.begin(), PowerArmorMgrInputHandler::GetSingleton());
        g_inputSinkRegistered = true;
    }
}

static fs::path GetGameRoot() {
    static fs::path p; static bool init = false;
    if (!init) {
        wchar_t b[MAX_PATH];
        GetModuleFileNameW(nullptr, b, MAX_PATH);
        p = fs::path(b).parent_path();
        init = true;
    }
    return p;
}

static fs::path PrimaryDataRoot() {
    return fs::path("Data") / DATA_DIR;
}

static std::string ReadUiLayoutJson() {
    const auto readProfile = [](const char* filename) -> std::string {
        for (const auto& path : { PrimaryDataRoot() / filename, fs::path(DATA_DIR) / filename }) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) continue;
            std::string raw{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            if (!raw.empty()) return raw;
        }
        return {};
    };

    const auto compact169 = readProfile("ui-layout-16x9-720p.json");
    const auto compact1610 = readProfile("ui-layout-16x10.json");
    const auto standard = readProfile("ui-layout-16x9-1080p-plus.json");
    if (!compact169.empty() && !compact1610.empty() && !standard.empty()) {
        return "{\"profiles\":{\"compact16x9\":" + compact169 +
            ",\"compact16x10\":" + compact1610 +
            ",\"standard\":" + standard + "}}";
    }

    const auto legacy = readProfile("ui-layout.json");
    return legacy.empty() ? "{}" : legacy;
}
static fs::path SlotPath(int s) {
    auto b = PrimaryDataRoot(); std::error_code ec; fs::create_directories(b, ec);
    char fn[32]; snprintf(fn, 32, "slot%03d.json", s); return b / fn;
}

static uint32_t HexToUInt(std::string_view value) {
    if (value.empty()) return 0;
    try {
        return static_cast<uint32_t>(std::stoul(std::string(value), nullptr, 16));
    } catch (...) {
        return 0;
    }
}

static RE::TESForm* ResolveSavedForm(std::string_view plugin, std::string_view localHex, std::string_view fullHex) {
    uint32_t localID = HexToUInt(localHex);
    if (!plugin.empty() && plugin != "Unknown.esm" && localID != 0) {
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            if (auto* form = data->LookupForm(localID, plugin)) {
                return form;
            }
            if (localID > 0xFFF) {
                if (auto* form = data->LookupForm(localID & 0xFFF, plugin)) {
                    return form;
                }
            }
        }
    }

    uint32_t formID = HexToUInt(fullHex);
    return formID != 0 ? RE::TESForm::GetFormByID(formID) : nullptr;
}

static bool WriteSlot(int s, int g, const std::vector<OE>& items) {
    if (s < 1 || s > MAX_SLOTS || items.empty()) return false;
    SavedSlot saved;
    saved.gender = g;
    saved.items = items;
    g_savedSlots[s] = std::move(saved);
    return true;
}
static bool ReadSlot(int s, int& g, std::vector<OE>& items, std::string& sum) {
    auto it = g_savedSlots.find(s);
    if (it == g_savedSlots.end() || it->second.items.empty()) return false;
    g = it->second.gender;
    items.clear();
    items = it->second.items;
    sum.clear();
    for (size_t i = 0; i < items.size() && i < 5; ++i) {
        if (i) sum += ", ";
        sum += items[i].name;
    }
    if (items.size() > 5) sum += "...";
    return !items.empty();
}
static bool DelSlot(int s) {
    g_savedSlots.erase(s);
    g_powerArmorRefs.erase(s);
    g_powerArmorRawRefs.erase(s);
    return true;
}

static void ClearAllSlotsMirror() {
    g_savedSlots.clear();
    g_powerArmorRefs.clear();
    g_powerArmorRawRefs.clear();
    g_saveDrafts.clear();
    g_index.clear();
    g_indexBySlot.clear();
    g_slotDetails.clear();
    g_randomBags.clear();
}

// ======== Index & State Persistence ========
static void RebuildIndexCache() {
    g_index.clear();
    g_indexBySlot.clear();
    g_slotDetails.clear();
    g_randomBags.clear();
    for (int i = 1; i <= MAX_SLOTS; ++i) {
        auto savedIt = g_savedSlots.find(i);
        if (savedIt != g_savedSlots.end() && savedIt->second.gender >= 0 && !savedIt->second.items.empty()) {
            SlotInfo info;
            info.slot = i;
            info.gender = savedIt->second.gender;
            info.count = static_cast<int>(savedIt->second.items.size());
            SlotDetail detail;
            for (std::size_t itemIndex = 0; itemIndex < savedIt->second.items.size() && itemIndex < 5; ++itemIndex) {
                if (itemIndex) detail.summary += ", ";
                detail.summary += savedIt->second.items[itemIndex].name;
            }
            if (savedIt->second.items.size() > 5) detail.summary += "...";
            for (const auto& item : savedIt->second.items) {
                detail.itemNames.push_back(item.name);
            }
            g_index.push_back(info);
            g_indexBySlot[i] = info;
            g_slotDetails[i] = std::move(detail);
        }
    }
}

static void WriteIndex() {
    RebuildIndexCache();
}

static void WriteState() {
}

static void LoadState() {
}

static void WriteActors() {
}

static void LoadActors() {
    std::lock_guard<std::mutex> lk(g_mx);
    g_mi.clear();
}

static void EnsureCacheLoaded() {
    if (g_cacheLoaded) return;
    WriteIndex();
    g_cacheLoaded = true;
}

static void SetMenuAction(const char* js, int action) {
    std::lock_guard<std::mutex> lk(g_mx);
    std::string_view j(js ? js : "");
    g_menuAct = action;
    g_menuActSlot = json::getInt(j, "slot", g_curSlot);
}

static void OnMenuStore(const char* js) { SetMenuAction(js, 1); }
static void OnMenuRestore(const char* js) { SetMenuAction(js, 2); }
static void OnMenuRandom(const char* js) { SetMenuAction(js, 3); }
static void OnMenuClearSlot(const char* js) { SetMenuAction(js, 4); }
static void OnMenuResetNpcOutfit(const char* js) { SetMenuAction(js, 5); }
static void OnMenuPrevSlot(const char* js) { SetMenuAction(js, 6); }
static void OnMenuNextSlot(const char* js) { SetMenuAction(js, 7); }
static void OnMenuSelectSlot(const char* js) { SetMenuAction(js, 8); }
static void OnMenuClose(const char*) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_prisma && g_prisma->IsValid(g_view)) {
        g_prisma->Unfocus(g_view);
        g_prisma->Hide(g_view);
    }
    g_menuOpen = false;
    g_hiddenBehindOverlay = false;
    g_menuTarget = {};
    SetGameplayInputBlocked(false);
    SetMenuTimePaused(false);
}

static void CloseResultView() {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_prisma && g_prisma->IsValid(g_resultView)) {
        g_prisma->Unfocus(g_resultView);
        g_prisma->Hide(g_resultView);
    }
    g_resultOpen = false;
    if (!g_menuOpen) {
        SetGameplayInputBlocked(false);
        SetMenuTimePaused(false);
    }
}

static void OnResultClose(const char*) {
    CloseResultView();
}

static void OnResultDomReady(PrismaView v) {
    if (!g_prisma) return;
    g_prisma->BindUIEvent(v, "onPowerArmorMgrResultClose", OnResultClose);
    g_resultViewReady = true;
    if (!g_pendingResultScript.empty()) {
        g_prisma->Invoke(v, g_pendingResultScript.c_str());
    }
}

static bool EnsureResultView() {
    if (!g_prisma) {
        g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>();
        if (!g_prisma) return false;
    }
    if (g_resultView == 0 || !g_prisma->IsValid(g_resultView)) {
        g_resultViewReady = false;
        g_pendingResultScript.clear();
        g_resultView = g_prisma->CreateView(RESULT_HTML, OnResultDomReady);
        if (g_resultView == 0) return false;
        g_prisma->SetOrder(g_resultView, -10);
    }
    return true;
}

static void OnDomReady(PrismaView v) {
    if (!g_prisma) return;
    g_prisma->BindUIEvent(v, "onPowerArmorMgrRestore", OnMenuRestore);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrStore", OnMenuStore);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrRandom", OnMenuRandom);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrClearSlot", OnMenuClearSlot);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrResetNpcOutfit", OnMenuResetNpcOutfit);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrPrevSlot", OnMenuPrevSlot);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrNextSlot", OnMenuNextSlot);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrSelectSlot", OnMenuSelectSlot);
    g_prisma->BindUIEvent(v, "onPowerArmorMgrClose", OnMenuClose);
    g_viewReady = true;
    if (!g_pendingMenuStateScript.empty()) {
        g_prisma->Invoke(v, g_pendingMenuStateScript.c_str());
    }
}
static bool EnsureView() {
    if (!g_prisma) { g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI4>(); if (!g_prisma) return false; }
    if (g_view == 0 || !g_prisma->IsValid(g_view)) {
        g_viewReady = false;
        g_pendingMenuStateScript.clear();
        g_view = g_prisma->CreateView(MENU_HTML, OnDomReady);
        if (g_view == 0) return false;
        g_prisma->SetOrder(g_view, -20);
    }
    return true;
}

static float Rem(float n, float l, float d) { float e = n - l; return e >= d ? 0 : d - e; }
static bool Try(float n, float& l, float d) { if (n - l < d) return false; l = n; return true; }

static int GetSex(RE::Actor* a) {
    if (!a) return -1; auto* npc = a->GetNPC(); return npc ? (int)npc->GetSex() : -1;
}
static std::string GetName(RE::Actor* a) {
    if (!a) return "Unknown";
    auto* pl = RE::PlayerCharacter::GetSingleton();
    if (a == pl) return "Player";
    auto* npc = a->GetNPC(); if (!npc) return "Unknown";
    auto* fn = npc->GetFullName(); return fn ? fn : "Unknown";
}
static bool IsArmor(RE::TESForm* f) {
    if (!f || !f->Is(RE::ENUM_FORM_ID::kARMO)) return false;
    auto* armor = f->As<RE::TESObjectARMO>();
    auto* name = armor ? armor->GetFullName() : nullptr;
    return name && name[0] != '\0';
}
static std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
static bool IsPowerArmor(RE::TESForm* f) {
    if (!f) return false;
    if (!f->Is(RE::ENUM_FORM_ID::kARMO)) {
        const auto fullName = Lower(std::string(RE::TESFullName::GetFullName(*f)));
        return fullName.find("power armor") != std::string::npos ||
               fullName.find("chassis") != std::string::npos ||
               fullName.find("frame") != std::string::npos;
    }
    auto* armor = f->As<RE::TESObjectARMO>();
    if (!armor) return false;
    if (armor->HasKeywordString("ArmorTypePower") ||
        armor->HasKeywordString("PowerArmorBodyPart") ||
        armor->HasKeywordString("PowerArmorFrame")) {
        return true;
    }
    if (auto* name = armor->GetFullName(); name && name[0] != '\0') {
        const auto lowerName = Lower(name);
        if (lowerName.find("power armor") != std::string::npos) {
            return true;
        }
    }
    return false;
}
static bool IsPipBoy(RE::TESForm* f) {
    if (!f || !f->Is(RE::ENUM_FORM_ID::kARMO)) return false;
    auto* armor = f->As<RE::TESObjectARMO>();
    if (!armor) return false;
    if (armor->HasKeywordString("Pipboy") ||
        armor->HasKeywordString("PipBoy") ||
        armor->HasKeywordString("ArmorTypePipboy") ||
        armor->HasKeywordString("ArmorTypePipBoy")) {
        return true;
    }
    if (auto* name = armor->GetFullName(); name && name[0] != '\0') {
        const auto lowerName = Lower(name);
        if (lowerName.find("pip-boy") != std::string::npos ||
            lowerName.find("pipboy") != std::string::npos ||
            lowerName.find("pip boy") != std::string::npos) {
            return true;
        }
    }
    return false;
}
static bool IsManagedOutfitArmor(RE::TESForm* f) {
    return IsArmor(f) && !IsPowerArmor(f) && !IsPipBoy(f);
}
static bool ReadBoolSettingFromFile(const fs::path& path, std::string_view key, bool fallback) {
    std::ifstream f(path);
    if (!f.is_open()) return fallback;
    std::string line;
    while (std::getline(f, line)) {
        auto semicolon = line.find(';');
        if (semicolon != std::string::npos) line.resize(semicolon);
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto k = line.substr(0, eq);
        k.erase(std::remove_if(k.begin(), k.end(), [](unsigned char c) { return std::isspace(c); }), k.end());
        if (k != key) continue;
        auto v = Lower(line.substr(eq + 1));
        v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c); }), v.end());
        return v == "1" || v == "true" || v == "yes" || v == "on";
    }
    return fallback;
}
static bool GetBoolSetting(std::string_view key, bool fallback) {
    const std::array<fs::path, 4> paths = {
        fs::path("Data/MCM/Settings/PowerArmorMgr.ini"),
        fs::path("Data/MCM/Config/PowerArmorMgr/settings.ini"),
        fs::path("MCM/Settings/PowerArmorMgr.ini"),
        fs::path("MCM/Config/PowerArmorMgr/settings.ini")
    };
    bool value = fallback;
    for (const auto& path : paths) {
        value = ReadBoolSettingFromFile(path, key, value);
    }
    return value;
}
static std::string Summ(int s) {
    int g = -1; std::vector<OE> items; std::string sum;
    if (ReadSlot(s, g, items, sum)) {
        if (!sum.empty()) return sum; if (items.empty()) return "(Empty)";
        std::string r; for (size_t i = 0; i < items.size() && i < 5; ++i) { if (i) r += ", "; r += items[i].name; }
        if (items.size() > 5) r += "..."; return r;
    } return "(Empty)";
}

// ======== Native Functions ========
RE::BSFixedString OM_GetPluginVersion(std::monostate) { return "1.0"; }
RE::BSFixedString OM_GetSlotPath(std::monostate, int) { return ""; }
bool OM_IsMenuAvailable(std::monostate) { return GetModuleHandleW(L"PrismaUI_F4.dll") != nullptr; }
int OM_GetMenuOpenBlockReason(std::monostate) { return MenuOpenBlockReason(); }

bool OM_OpenMenu(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    RegisterInputSink();
    StartOverlayMonitor();
    if (MenuOpenBlockReason() != 0) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    if (!EnsureView() || !g_prisma->IsValid(g_view)) return false;
    g_menuTarget = t ? t->GetHandle() : RE::ObjectRefHandle{};
    g_menuOpen = true; g_menuAct = 0; g_menuActSlot = 0;
    g_hiddenBehindOverlay = false;
    SetGameplayInputBlocked(true);
    SetMenuTimePaused(true);
    int slot = g_curSlot;
    bool hd = false;
    int sg = -1;
    std::string ss;
    std::vector<std::string> currentItems;
    if (auto it = g_indexBySlot.find(slot); it != g_indexBySlot.end()) {
        hd = true;
        sg = it->second.gender;
        if (auto detailIt = g_slotDetails.find(slot); detailIt != g_slotDetails.end()) {
            ss = detailIt->second.summary;
            currentItems = detailIt->second.itemNames;
        }
    }

    std::string summaries = "{";
    for (const auto& info : g_index) {
        if (summaries.size() > 1) {
            summaries += ",";
        }
        std::string slotItemsJson = "[";
        if (auto detailIt = g_slotDetails.find(info.slot); detailIt != g_slotDetails.end()) {
            const auto& names = detailIt->second.itemNames;
            for (std::size_t itemIndex = 0; itemIndex < names.size(); ++itemIndex) {
                if (itemIndex) {
                    slotItemsJson += ",";
                }
                slotItemsJson += "\"" + json::esc(names[itemIndex]) + "\"";
            }
        }
        slotItemsJson += "]";

        summaries += "\"" + std::to_string(info.slot) + "\":{\"hasData\":true,\"gender\":" +
                     std::to_string(info.gender) + ",\"count\":" + std::to_string(info.count) +
                     ",\"items\":" + slotItemsJson + "}";
    }
    summaries += "}";

    std::string itemsJson = "[";
    for (std::size_t i = 0; i < currentItems.size(); ++i) {
        if (i) {
            itemsJson += ",";
        }
        itemsJson += "\"" + json::esc(currentItems[i]) + "\"";
    }
    itemsJson += "]";

    std::string state = "{\"slot\":" + std::to_string(slot) +
                        ",\"maxSlot\":" + std::to_string(MAX_SLOTS) +
                        ",\"hasData\":" + (hd ? "true" : "false") +
                        ",\"slotGender\":" + std::to_string(sg) +
                        ",\"summary\":\"" + json::esc(ss) +
                        "\",\"items\":" + itemsJson +
                        ",\"slotSummaries\":" + summaries + "}";

    std::string sc = "if(window.setPowerArmorMgrLayout)window.setPowerArmorMgrLayout(" + ReadUiLayoutJson() + ");";
    sc += "if(window.setPowerArmorMgrState)window.setPowerArmorMgrState(" + state + ");";
    g_pendingMenuStateScript = sc;
    g_prisma->Show(g_view); g_prisma->Focus(g_view, false);
    if (g_viewReady) {
        g_prisma->Invoke(g_view, g_pendingMenuStateScript.c_str());
    }
    return true;
}
void OM_CloseMenu(std::monostate) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_prisma && g_prisma->IsValid(g_view)) { g_prisma->Unfocus(g_view); g_prisma->Hide(g_view); }
    g_menuOpen = false; g_menuTarget = {}; g_menuAct = 0; g_menuActSlot = 0;
    g_hiddenBehindOverlay = false;
    SetGameplayInputBlocked(false);
    SetMenuTimePaused(false);
}

bool OM_ShowResult(std::monostate, RE::BSFixedString asKind, RE::BSFixedString asTitle,
                   RE::BSFixedString asBody, bool abError) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!EnsureResultView() || !g_prisma->IsValid(g_resultView)) return false;

    const std::string kind = asKind.c_str() ? asKind.c_str() : "warning";
    const std::string title = asTitle.c_str() ? asTitle.c_str() : "Power Armor Storage";
    const std::string body = asBody.c_str() ? asBody.c_str() : "";
    g_pendingResultScript = "if(window.setPowerArmorMgrResult)window.setPowerArmorMgrResult({\"kind\":\"" +
        json::esc(kind) + "\",\"title\":\"" + json::esc(title) + "\",\"body\":\"" +
        json::esc(body) + "\",\"error\":" + (abError ? "true" : "false") + "});";
    g_resultOpen = true;
    SetGameplayInputBlocked(true);
    SetMenuTimePaused(true);
    g_prisma->Show(g_resultView);
    g_prisma->Focus(g_resultView, false);
    if (g_resultViewReady) {
        g_prisma->Invoke(g_resultView, g_pendingResultScript.c_str());
    }
    return true;
}

bool OM_IsMenuOpen(std::monostate) { UpdateOverlayCoverState(); return g_menuOpen; }
RE::Actor* OM_GetMenuTarget(std::monostate) { return g_menuTarget ? g_menuTarget.get()->As<RE::Actor>() : nullptr; }
void OM_ClearMenuTarget(std::monostate) { g_menuTarget = {}; }
float OM_GetMenuOpenCooldownRemaining(std::monostate, float n) { return Rem(n, g_menuOpenT, 1.0f); }
bool OM_BeginMenuOpen(std::monostate, float n, float d) { return Try(n, g_menuOpenT, d); }
float OM_GetMenuActionCooldownRemaining(std::monostate, float n) { return Rem(n, g_menuActionT, 0.6f); }
bool OM_BeginMenuAction(std::monostate, float n, float d) { return Try(n, g_menuActionT, d); }
int OM_GetMenuAction(std::monostate) { return g_menuAct; }
int OM_GetMenuActionSlot(std::monostate) { return g_menuActSlot; }
void OM_ClearMenuAction(std::monostate) { g_menuAct = 0; g_menuActSlot = 0; }
RE::Actor* OM_GetCameraTarget(std::monostate) { return nullptr; }
RE::BSFixedString OM_GetActorName(std::monostate, RE::Actor* a) { return GetName(a).c_str(); }
bool OM_IsValidTarget(std::monostate, RE::Actor* a) { return a && !a->IsDead(false); }
bool OM_IsOutfitItem(std::monostate, RE::TESForm* f) { return IsManagedOutfitArmor(f); }
bool OM_IsPowerArmorItem(std::monostate, RE::TESForm* f) { return IsPowerArmor(f); }
bool OM_AreOutfitNotificationsEnabled(std::monostate) { return GetBoolSetting("bEnableOutfitNotifications", false); }

int OM_GetCurrentSlot(std::monostate) { return g_curSlot; }
void OM_SetCurrentSlot(std::monostate, int s) { if (s >= 1 && s <= MAX_SLOTS) { g_curSlot = s; WriteState(); } }
int OM_GetLastRandomSlot(std::monostate) { return g_lastRandSlot; }
int OM_GetLastEquippedSlot(std::monostate) { return g_lastEquippedSlot; }
void OM_SetLastEquippedSlot(std::monostate, int s) { if (s >= 1 && s <= MAX_SLOTS) g_lastEquippedSlot = s; }
bool OM_IsEquipBusy(std::monostate) { return g_eqBusy; }
float OM_GetEquipCooldownRemaining(std::monostate, float n) { return Rem(n, g_eqLastT, 3.0f); }
bool OM_BeginEquipAction(std::monostate, float n, float d) { if (g_eqBusy) return false; if (!Try(n, g_eqLastT, d)) return false; g_eqBusy = true; return true; }
void OM_FinishEquipAction(std::monostate) { g_eqBusy = false; }
bool OM_IsRandomBusy(std::monostate) { return g_randBusy; }
float OM_GetRandomCooldownRemaining(std::monostate, float n) { return Rem(n, g_randLastT, 6.0f); }
bool OM_BeginRandom(std::monostate, float n, float d) {
    if (g_randBusy || g_eqBusy) return false;
    if (!Try(n, g_randLastT, d)) return false;
    g_randBusy = true;
    g_eqBusy = true;
    return true;
}
void OM_FinishRandom(std::monostate, int s) { g_randBusy = false; g_eqBusy = false; if (s > 0) g_lastRandSlot = s; }

int OM_ChooseRandomSlot(std::monostate, int sex, int cur, int last) {
    EnsureCacheLoaded();
    std::vector<int> eligible;
    for (const auto& info : g_index) {
        if (info.slot == cur || info.slot == last) continue;
        if (info.gender == sex && info.count > 0) eligible.push_back(info.slot);
    }
    if (eligible.empty() && last > 0) {
        for (const auto& info : g_index) {
            if (info.slot == cur) continue;
            if (info.gender == sex && info.count > 0) eligible.push_back(info.slot);
        }
    }
    if (eligible.empty()) return 0;

    auto& bag = g_randomBags[sex];
    bag.erase(std::remove_if(bag.begin(), bag.end(), [&](int slot) {
        return std::find(eligible.begin(), eligible.end(), slot) == eligible.end();
    }), bag.end());

    if (bag.empty()) {
        bag = eligible;
        static std::mt19937 rng{ std::random_device{}() };
        std::shuffle(bag.begin(), bag.end(), rng);
    }

    LARGE_INTEGER perf{};
    QueryPerformanceCounter(&perf);
    const auto rotate = bag.empty() ? 0 : static_cast<std::size_t>(perf.QuadPart) % bag.size();
    std::rotate(bag.begin(), bag.begin() + rotate, bag.end());
    const int chosen = bag.back();
    bag.pop_back();
    return chosen;
}
int OM_FindMatchingSlot(std::monostate, int cur, int dir, int sex) {
    EnsureCacheLoaded();
    if (dir == 0) return 0;
    if (g_index.empty()) return 0;

    int bestSlot = 0;
    if (dir > 0) {
        int wrapSlot = 0;
        for (const auto& info : g_index) {
            if (info.gender != sex || info.count <= 0) continue;
            if (info.slot > cur && (bestSlot == 0 || info.slot < bestSlot)) bestSlot = info.slot;
            if (wrapSlot == 0 || info.slot < wrapSlot) wrapSlot = info.slot;
        }
        return bestSlot != 0 ? bestSlot : wrapSlot;
    }

    int wrapSlot = 0;
    for (const auto& info : g_index) {
        if (info.gender != sex || info.count <= 0) continue;
        if (info.slot < cur && info.slot > bestSlot) bestSlot = info.slot;
        if (info.slot > wrapSlot) wrapSlot = info.slot;
    }
    return bestSlot != 0 ? bestSlot : wrapSlot;
}
bool OM_HasSlotData(std::monostate, int s) { EnsureCacheLoaded(); return g_indexBySlot.find(s) != g_indexBySlot.end(); }
int OM_GetSlotGender(std::monostate, int s) {
    EnsureCacheLoaded();
    auto it = g_indexBySlot.find(s);
    return it == g_indexBySlot.end() ? -1 : it->second.gender;
}

static void AddOutfitEntry(std::vector<OE>& items, RE::TESForm* form) {
    if (!IsManagedOutfitArmor(form)) return;
    uint32_t fid = form->GetFormID();
    for (auto& e : items) {
        if (e.fid == fid) return;
    }

    OE e;
    e.fid = fid;
    auto* arm = form->As<RE::TESObjectARMO>();
    auto* fn = arm ? arm->GetFullName() : nullptr;
    e.name = fn ? fn : "Unknown";
    items.push_back(e);
}

static void AddPowerArmorEntry(std::vector<OE>& items, RE::TESForm* form) {
    if (!IsPowerArmor(form)) return;
    uint32_t fid = form->GetFormID();
    for (auto& e : items) {
        if (e.fid == fid) return;
    }

    OE e;
    e.fid = fid;
    auto* arm = form->As<RE::TESObjectARMO>();
    auto* fn = arm ? arm->GetFullName() : nullptr;
    e.name = fn ? fn : "Unknown Power Armor Part";
    items.push_back(e);
}

static std::vector<OE> CollectPowerArmorParts(RE::TESObjectREFR* frame) {
    std::vector<OE> items;
    if (!frame || !frame->inventoryList) return items;

    for (auto& item : frame->inventoryList->data) {
        auto* form = item.object;
        if (!IsPowerArmor(form)) continue;
        AddPowerArmorEntry(items, form);
    }

    if (items.empty()) {
        items.push_back({ 0, "Power Armor Frame" });
    }
    return items;
}

static bool IsPowerArmorReference(RE::TESObjectREFR* ref) {
    if (!ref) return false;
    if (IsPowerArmor(ref->GetObjectReference())) return true;

    if (!ref->inventoryList) return false;
    for (auto& item : ref->inventoryList->data) {
        if (IsPowerArmor(item.object)) {
            return true;
        }
    }
    return false;
}

static bool HandleMatchesReference(RE::ObjectRefHandle handle, const RE::TESObjectREFR* target) {
    if (!handle || !target) return false;
    auto ref = handle.get();
    return ref && ref->GetFormID() == target->GetFormID();
}

static bool IsPowerArmorOccupied(RE::TESObjectREFR* frame) {
    if (!frame) return false;
    auto* cell = frame->GetParentCell();
    if (!cell) return false;

    bool occupied = false;
    cell->ForEachReferenceInRange(frame->GetPosition(), 512.0F, [&](RE::TESObjectREFR* ref) {
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor || !actor->currentProcess) return RE::BSContainer::ForEachResult::kContinue;

        if (HandleMatchesReference(actor->currentProcess->GetOccupiedFurniture(), frame)) {
            occupied = true;
            return RE::BSContainer::ForEachResult::kStop;
        }
        if (auto* middleHigh = actor->currentProcess->middleHigh;
            middleHigh && (HandleMatchesReference(middleHigh->currentFurniture, frame) ||
                           HandleMatchesReference(middleHigh->occupiedFurniture, frame))) {
            occupied = true;
            return RE::BSContainer::ForEachResult::kStop;
        }
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return occupied;
}

static bool IsUnownedOrPlayerOwned(RE::TESObjectREFR* frame) {
    if (!frame) return false;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return false;
    if (!frame->GetHasOwner() && frame->GetOwner() == nullptr) return true;
    return frame->IsAnOwner(player, true, true);
}

static bool WritePowerArmorSlot(int s, RE::TESObjectREFR* frame, const std::vector<OE>& items) {
    if (!frame || s < 1 || s > MAX_SLOTS || items.empty()) return false;
    SavedSlot saved;
    saved.gender = 0;
    saved.frameID = frame->GetFormID();
    const char* displayName = frame->GetDisplayFullName();
    saved.frameName = displayName && displayName[0] ? displayName : "Power Armor";
    saved.items = items;
    g_savedSlots[s] = std::move(saved);
    return true;
}

static RE::TESObjectREFR* ResolveSavedPowerArmorRef(int slot) {
    if (auto rawIt = g_powerArmorRawRefs.find(slot); rawIt != g_powerArmorRawRefs.end() && rawIt->second) {
        return rawIt->second;
    }

    if (auto it = g_powerArmorRefs.find(slot); it != g_powerArmorRefs.end()) {
        if (it->second) {
            auto ref = it->second.get();
            if (ref) {
                return ref.get();
            }
        }
        g_powerArmorRefs.erase(it);
    }

    auto savedIt = g_savedSlots.find(slot);
    if (savedIt == g_savedSlots.end() || savedIt->second.frameID == 0) return nullptr;
    auto* form = RE::TESForm::GetFormByID(savedIt->second.frameID);
    return form ? form->As<RE::TESObjectREFR>() : nullptr;
}

static RE::TESObjectREFR* ResolveNativePowerArmorTarget() {
    auto resolveHandle = [](RE::ObjectRefHandle handle) -> RE::TESObjectREFR* {
        if (!handle) return nullptr;
        auto ref = handle.get();
        return ref && IsPowerArmorReference(ref.get()) ? ref.get() : nullptr;
    };

    if (auto* ref = resolveHandle(RE::Console::GetCurrentPickREFR())) {
        return ref;
    }
    if (auto* ref = resolveHandle(RE::Console::GetPickRef())) {
        return ref;
    }
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        if (auto* ref = resolveHandle(player->forceActivateRef)) {
            return ref;
        }
        if (auto* ref = resolveHandle(player->loopingActivateRef)) {
            return ref;
        }
    }
    return nullptr;
}

static bool MovePowerArmorToPlayer(RE::TESObjectREFR* frame, RE::Actor* player) {
    if (!frame || !player) return false;

    auto* cell = player->parentCell;
    if (!cell) return false;
    if (!frame->IsDisabled()) frame->Disable();
    if (frame->parentCell != cell) {
        frame->MoveRefToNewSpace(cell, cell->IsExterior() ? cell->worldSpace : nullptr);
    }

    const float angleZ = player->data.angle.z;
    RE::NiPoint3 loc = player->data.location;
    loc.x += std::sin(angleZ) * 190.0F;
    loc.y += std::cos(angleZ) * 190.0F;
    loc.z += 12.0F;

    frame->SetLocationOnReference(loc);
    frame->SetAngleOnReference(RE::NiPoint3{ 0.0F, 0.0F, angleZ });
    if (frame->parentCell != cell) {
        frame->Disable();
        return false;
    }

    const auto actual = frame->GetPosition();
    const float errorSq = std::pow(actual.x - loc.x, 2.0F) +
                          std::pow(actual.y - loc.y, 2.0F) +
                          std::pow(actual.z - loc.z, 2.0F);
    if (errorSq > 4096.0F) {
        frame->Disable();
        return false;
    }

    frame->Enable(false);
    frame->UpdateReference3D();
    if (frame->IsDisabled()) {
        frame->Disable();
        return false;
    }
    return true;
}

static int FindSlotByPowerArmorRef(RE::TESObjectREFR* frame) {
    if (!frame) return 0;
    const auto frameID = frame->GetFormID();
    for (const auto& [slot, saved] : g_savedSlots) {
        if (saved.frameID == frameID) return slot;
    }
    return 0;
}

static std::vector<OE> CollectEquippedArmor(RE::Actor* actor) {
    std::vector<OE> items;
    if (!actor) return items;

    if (const auto& biped = actor->GetCurrentBiped(); biped) {
        for (int i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
            auto* bip = biped->GetBipObject(static_cast<RE::BIPED_OBJECT>(i));
            if (bip && bip->parent.object) {
                AddOutfitEntry(items, bip->parent.object);
            }
        }
    }

    if (!items.empty()) {
        return items;
    }

    auto* inv = actor->inventoryList;
    if (!inv) return items;
    for (auto& item : inv->data) {
        auto* form = item.object;
        if (!IsManagedOutfitArmor(form)) continue;
        bool equipped = false;
        for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
            if (stack->IsEquipped()) { equipped = true; break; }
        }
        if (equipped) {
            AddOutfitEntry(items, form);
        }
    }
    return items;
}

int OM_SaveOutfit(std::monostate, int slot, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t || slot < 1 || slot > MAX_SLOTS) return 0;
    int sex = GetSex(t); if (sex < 0) return 0;
    auto items = CollectEquippedArmor(t);
    if (items.empty()) return 0;
    WriteSlot(slot, sex, items); WriteIndex(); return (int)items.size();
}

bool OM_BeginSaveOutfit(std::monostate, int slot, int gender) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS || gender < 0) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    auto& draft = g_saveDrafts[slot];
    draft.gender = gender;
    draft.items.clear();
    return true;
}

bool OM_AddSaveOutfitItem(std::monostate, int slot, RE::TESForm* item) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS || !IsManagedOutfitArmor(item)) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_saveDrafts.find(slot);
    if (it == g_saveDrafts.end()) return false;
    AddOutfitEntry(it->second.items, item);
    return true;
}

int OM_CommitSaveOutfit(std::monostate, int slot) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS) return 0;

    SaveDraft draft;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_saveDrafts.find(slot);
        if (it == g_saveDrafts.end()) return 0;
        draft = it->second;
        g_saveDrafts.erase(it);
    }

    if (draft.gender < 0 || draft.items.empty()) return 0;
    if (!WriteSlot(slot, draft.gender, draft.items)) return 0;
    WriteIndex();
    return static_cast<int>(draft.items.size());
}

int OM_LoadOutfit(std::monostate, int slot, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return 0;
    int sg = -1; std::vector<OE> items; std::string sum;
    if (!ReadSlot(slot, sg, items, sum)) return -1;
    int ts = GetSex(t);
    if (ts >= 0 && sg >= 0 && ts != sg) return -2;
    int resolvedCount = 0;
    for (auto& item : items) {
        auto* form = RE::TESForm::GetFormByID(item.fid);
        if (IsManagedOutfitArmor(form)) {
            ++resolvedCount;
            if (resolvedCount >= 44) break;
        }
    }
    return resolvedCount > 0 ? resolvedCount : -1;
}

int OM_SavePowerArmor(std::monostate, int slot, RE::TESObjectREFR* frame) {
    EnsureCacheLoaded();
    if (!IsPowerArmorReference(frame) || slot < 1 || slot > MAX_SLOTS) return 0;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || player->IsInCombat()) return -5;
    if (g_savedSlots.contains(slot)) return -2;
    if (IsPowerArmorOccupied(frame)) return -3;
    if (!IsUnownedOrPlayerOwned(frame)) return -4;

    int existingSlot = FindSlotByPowerArmorRef(frame);
    if (existingSlot > 0) return -6;

    auto items = CollectPowerArmorParts(frame);
    if (!WritePowerArmorSlot(slot, frame, items)) return 0;
    g_powerArmorRawRefs[slot] = frame;
    g_powerArmorRefs[slot] = frame->GetHandle();
    frame->Disable();
    if (!frame->IsDisabled()) {
        DelSlot(slot);
        WriteIndex();
        return 0;
    }
    WriteIndex();
    return static_cast<int>(items.size());
}

RE::TESObjectREFR* OM_GetSavedPowerArmorRef(std::monostate, int slot) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS) return nullptr;
    return ResolveSavedPowerArmorRef(slot);
}

RE::TESObjectREFR* OM_GetLookedAtPowerArmor(std::monostate) {
    EnsureCacheLoaded();
    return ResolveNativePowerArmorTarget();
}

int OM_DeployPowerArmor(std::monostate, int slot, RE::Actor* player) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS || !player) return -1;
    if (player->IsInCombat()) return -5;
    auto* frame = ResolveSavedPowerArmorRef(slot);
    if (!frame) return 0;
    if (!MovePowerArmorToPlayer(frame, player)) return 0;
    DelSlot(slot);
    WriteIndex();
    return 1;
}

int OM_FindPowerArmorSlot(std::monostate, RE::TESObjectREFR* frame) {
    EnsureCacheLoaded();
    return FindSlotByPowerArmorRef(frame);
}

bool OM_IsPowerArmorReference(std::monostate, RE::TESObjectREFR* ref) {
    return IsPowerArmorReference(ref);
}

RE::TESForm* OM_GetSavedOutfitItem(std::monostate, int s, int idx) {
    (void)s;
    (void)idx;
    return nullptr;
}
int OM_GetSavedOutfitItemID(std::monostate, int s, int idx) {
    EnsureCacheLoaded();
    int g = -1; std::vector<OE> items; std::string sum;
    if (!ReadSlot(s, g, items, sum)) return 0;
    if (idx < 0) return 0;
    int resolvedIndex = 0;
    for (auto& item : items) {
        auto* form = RE::TESForm::GetFormByID(item.fid);
        if (!IsManagedOutfitArmor(form)) continue;
        if (resolvedIndex == idx) return static_cast<int>(form->GetFormID());
        ++resolvedIndex;
    }
    return 0;
}
RE::BSFixedString OM_GetSavedOutfitDebug(std::monostate, int s) {
    EnsureCacheLoaded();
    int g = -1; std::vector<OE> items; std::string sum;
    if (!ReadSlot(s, g, items, sum)) return "(Empty)";
    std::string r = "Slot " + std::to_string(s) + " G:" + std::to_string(g) + "\n";
    for (size_t i = 0; i < items.size(); ++i) {
        char b[32]; snprintf(b, 32, "%08X", items[i].fid);
        r += "  [" + std::to_string(i) + "] " + items[i].name + " (0x" + b + ")\n";
    } return r.c_str();
}
RE::BSFixedString OM_GetSavedOutfitSummary(std::monostate, int s) { EnsureCacheLoaded(); return Summ(s).c_str(); }
bool OM_ClearSlotData(std::monostate, int s) { EnsureCacheLoaded(); bool ok = DelSlot(s); if (ok) WriteIndex(); return ok; }

bool OM_EnsureDefaultOutfitRecorded(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    auto& mi = g_mi[t->GetFormID()];
    if (mi.dr) return true;
    auto equipped = CollectEquippedArmor(t);
    for (auto& item : equipped) {
        uint32_t fid = item.fid;
        bool dup = false;
        for (auto id : mi.d) { if (id == fid) { dup = true; break; } }
        if (!dup) mi.d.push_back(fid);
    }
    mi.dr = true; return true;
}
bool OM_RecordManagedItem(std::monostate, RE::Actor* t, RE::TESForm* item) {
    EnsureCacheLoaded();
    if (!t || !item) return false;
    std::lock_guard<std::mutex> lk(g_mx); auto& mi = g_mi[t->GetFormID()];
    uint32_t iid = item->GetFormID();
    for (auto id : mi.m) if (id == iid) return false;
    mi.m.push_back(iid); WriteActors(); return true;
}
int OM_GetManagedItemCount(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); return it == g_mi.end() ? 0 : (int)it->second.m.size();
}
RE::TESForm* OM_GetManagedItem(std::monostate, RE::Actor* t, int idx) {
    (void)t;
    (void)idx;
    return nullptr;
}
int OM_GetManagedItemID(std::monostate, RE::Actor* t, int idx) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); if (it == g_mi.end()) return 0;
    if (idx < 0 || idx >= (int)it->second.m.size()) return 0;
    return static_cast<int>(it->second.m[idx]);
}
void OM_ClearManagedItems(std::monostate, RE::Actor* t) { EnsureCacheLoaded(); if (t) { std::lock_guard<std::mutex> lk(g_mx); g_mi.erase(t->GetFormID()); WriteActors(); } }
int OM_GetDefaultOutfitCount(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); return it == g_mi.end() ? 0 : (int)it->second.d.size();
}
RE::TESForm* OM_GetDefaultOutfitItem(std::monostate, RE::Actor* t, int idx) {
    (void)t;
    (void)idx;
    return nullptr;
}
int OM_GetDefaultOutfitItemID(std::monostate, RE::Actor* t, int idx) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); if (it == g_mi.end()) return 0;
    if (idx < 0 || idx >= (int)it->second.d.size()) return 0;
    return static_cast<int>(it->second.d[idx]);
}
RE::BSFixedString OM_DebugGetInventoryInfo(std::monostate, RE::Actor* t) {
    if (!t) return "No target";
    std::string r = "Inv: " + GetName(t) + "\n";
    auto* inv = t->inventoryList;
    if (!inv) return (r + "(no inv)").c_str();
    for (auto& item : inv->data) {
        auto* form = item.object;
        if (IsManagedOutfitArmor(form)) {
            bool equipped = false;
            for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (stack->IsEquipped()) { equipped = true; break; }
            }
            if (equipped) {
                auto* ar = form->As<RE::TESObjectARMO>();
                auto* fn = ar ? ar->GetFullName() : nullptr;
                r += "  " + std::string(fn ? fn : "?") + "\n";
            }
        }
    }
    return r.c_str();
}
void OM_RefreshActorModel(std::monostate, RE::Actor*) { /* No-op */ }

static bool WriteStringRecord(const F4SE::SerializationInterface* intfc, const std::string& value) {
    const uint32_t len = static_cast<uint32_t>(value.size());
    return intfc->WriteRecordData(len) && (len == 0 || intfc->WriteRecordData(value.data(), len));
}

static bool ReadStringRecord(const F4SE::SerializationInterface* intfc, std::string& value) {
    uint32_t len = 0;
    if (intfc->ReadRecordData(len) != sizeof(len)) return false;
    if (len > 4096) return false;
    value.clear();
    if (len == 0) return true;
    value.resize(len);
    return intfc->ReadRecordData(value.data(), len) == len;
}

static void F4SEAPI OnSerializationSave(const F4SE::SerializationInterface* intfc) {
    EnsureCacheLoaded();
    if (!intfc->OpenRecord(SERIAL_RECORD_SLOTS, SERIAL_VERSION)) return;

    std::vector<int> slots;
    for (const auto& [slot, saved] : g_savedSlots) {
        if (slot >= 1 && slot <= MAX_SLOTS && saved.frameID != 0 && !saved.items.empty()) {
            slots.push_back(slot);
        }
    }

    const uint32_t currentSlot = static_cast<uint32_t>(g_curSlot);
    const uint32_t count = static_cast<uint32_t>(slots.size());
    intfc->WriteRecordData(currentSlot);
    intfc->WriteRecordData(count);

    for (int slot : slots) {
        const auto& saved = g_savedSlots[slot];
        const uint32_t slotValue = static_cast<uint32_t>(slot);
        const uint32_t gender = static_cast<uint32_t>(saved.gender > 0 ? saved.gender : 0);
        const uint32_t itemCount = static_cast<uint32_t>(saved.items.size());
        intfc->WriteRecordData(slotValue);
        intfc->WriteRecordData(gender);
        intfc->WriteRecordData(saved.frameID);
        WriteStringRecord(intfc, saved.frameName);
        intfc->WriteRecordData(itemCount);
        for (const auto& item : saved.items) {
            intfc->WriteRecordData(item.fid);
            WriteStringRecord(intfc, item.name);
        }
    }
}

static void ResetRuntimeStorage() {
    g_curSlot = 1;
    g_lastRandSlot = 0;
    g_lastEquippedSlot = 0;
    g_eqBusy = false;
    g_randBusy = false;
    g_menuAct = 0;
    g_menuActSlot = 0;
    ClearAllSlotsMirror();
}

static void F4SEAPI OnSerializationRevert(const F4SE::SerializationInterface*) {
    std::lock_guard<std::mutex> lk(g_mx);
    ResetRuntimeStorage();
    WriteIndex();
    g_cacheLoaded = true;
}

static void F4SEAPI OnSerializationLoad(const F4SE::SerializationInterface* intfc) {
    {
        std::lock_guard<std::mutex> lk(g_mx);
        ResetRuntimeStorage();
    }

    uint32_t type = 0;
    uint32_t version = 0;
    uint32_t length = 0;
    while (intfc->GetNextRecordInfo(type, version, length)) {
        if (type != SERIAL_RECORD_SLOTS || version != SERIAL_VERSION) {
            std::vector<char> skip(length);
            if (length > 0) intfc->ReadRecordData(skip.data(), length);
            continue;
        }

        uint32_t currentSlot = 1;
        uint32_t count = 0;
        if (intfc->ReadRecordData(currentSlot) != sizeof(currentSlot)) break;
        if (intfc->ReadRecordData(count) != sizeof(count)) break;
        if (currentSlot >= 1 && currentSlot <= MAX_SLOTS) {
            g_curSlot = static_cast<int>(currentSlot);
        }

        for (uint32_t i = 0; i < count; ++i) {
            uint32_t slotValue = 0;
            uint32_t gender = 0;
            uint32_t frameID = 0;
            uint32_t itemCount = 0;
            std::string frameName;
            if (intfc->ReadRecordData(slotValue) != sizeof(slotValue)) break;
            if (intfc->ReadRecordData(gender) != sizeof(gender)) break;
            if (intfc->ReadRecordData(frameID) != sizeof(frameID)) break;
            if (!ReadStringRecord(intfc, frameName)) break;
            if (intfc->ReadRecordData(itemCount) != sizeof(itemCount)) break;

            SavedSlot saved;
            saved.gender = static_cast<int>(gender);
            saved.frameName = frameName;
            if (auto resolvedFrame = intfc->ResolveFormID(frameID)) {
                saved.frameID = *resolvedFrame;
            } else {
                saved.frameID = frameID;
            }

            for (uint32_t itemIndex = 0; itemIndex < itemCount; ++itemIndex) {
                uint32_t itemID = 0;
                std::string itemName;
                if (intfc->ReadRecordData(itemID) != sizeof(itemID)) break;
                if (!ReadStringRecord(intfc, itemName)) break;
                uint32_t resolvedItemID = itemID;
                if (auto resolvedItem = intfc->ResolveFormID(itemID)) {
                    resolvedItemID = *resolvedItem;
                }
                saved.items.push_back({ resolvedItemID, itemName });
            }

            if (slotValue < 1 || slotValue > MAX_SLOTS || saved.frameID == 0 || saved.items.empty()) continue;
            const int slot = static_cast<int>(slotValue);
            g_savedSlots[slot] = saved;
            auto* form = RE::TESForm::GetFormByID(saved.frameID);
            auto* frame = form ? form->As<RE::TESObjectREFR>() : nullptr;
            if (frame) {
                g_powerArmorRawRefs[slot] = frame;
                g_powerArmorRefs[slot] = frame->GetHandle();
            }
        }
    }

    WriteIndex();
    g_cacheLoaded = true;
}

static void RegisterSerialization() {
    if (auto* serialization = F4SE::GetSerializationInterface()) {
        serialization->SetUniqueID(SERIAL_UID);
        serialization->SetSaveCallback(OnSerializationSave);
        serialization->SetLoadCallback(OnSerializationLoad);
        serialization->SetRevertCallback(OnSerializationRevert);
    }
}

// ======== Registration ========
bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm) {
    if (!vm) return false;
    #define B(name) vm->BindNativeMethod("PANative", #name, OM_##name)
    B(GetPluginVersion); B(GetSlotPath); B(GetActorName);
    B(GetSavedOutfitDebug); B(GetSavedOutfitSummary); B(DebugGetInventoryInfo);
    B(IsMenuAvailable); B(GetMenuOpenBlockReason); B(OpenMenu); B(CloseMenu); B(ShowResult); B(IsMenuOpen);
    B(GetMenuOpenCooldownRemaining); B(BeginMenuOpen);
    B(GetMenuActionCooldownRemaining); B(BeginMenuAction);
    B(IsValidTarget); B(IsOutfitItem); B(IsPowerArmorItem); B(IsPowerArmorReference); B(AreOutfitNotificationsEnabled); B(IsEquipBusy);
    B(GetEquipCooldownRemaining); B(BeginEquipAction);
    B(IsRandomBusy); B(GetRandomCooldownRemaining); B(BeginRandom);
    B(HasSlotData); B(ClearSlotData); B(EnsureDefaultOutfitRecorded);
    B(RecordManagedItem); B(GetMenuAction); B(GetMenuActionSlot);
    B(GetCurrentSlot); B(GetLastRandomSlot); B(GetLastEquippedSlot); B(ChooseRandomSlot);
    B(FindMatchingSlot); B(GetSlotGender); B(GetSavedOutfitItemID); B(GetManagedItemCount);
    B(GetDefaultOutfitCount); B(SaveOutfit); B(BeginSaveOutfit);
    B(AddSaveOutfitItem); B(CommitSaveOutfit); B(LoadOutfit);
    B(SavePowerArmor); B(GetSavedPowerArmorRef); B(GetLookedAtPowerArmor); B(DeployPowerArmor); B(FindPowerArmorSlot);
    B(GetMenuTarget); B(GetCameraTarget);
    B(GetManagedItemID); B(GetDefaultOutfitItemID);
    B(ClearMenuTarget); B(ClearMenuAction); B(SetCurrentSlot); B(SetLastEquippedSlot);
    B(FinishEquipAction); B(FinishRandom); B(ClearManagedItems);
    B(RefreshActorModel);
    #undef B
    return true;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_intfc) {
    F4SE::Init(a_intfc, { .log = false, .hook = false });
    if (auto* papyrus = F4SE::GetPapyrusInterface()) {
        papyrus->Register(RegisterPapyrus);
    }
    RegisterSerialization();
    RegisterInputSink();
    StartOverlayMonitor();
    return true;
}
POWERARMORMGR_EXPORT bool F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info) {
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "PowerArmorMgr"; a_info->version = 1;
    if (a_f4se->IsEditor()) return false;
    return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_162;
}
