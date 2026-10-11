// Power Armor Manager 3.0 - core-only experimental rewrite.
// No PrismaUI, SWF, UI polling threads, or outfit-management APIs.
// IMPORTANT: Requires real game testing before distribution.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <variant>
#include "SlotCodec.h"

#include "RE/Fallout.h"
#include "F4SE/F4SE.h"
#include "F4SE/API.h"

namespace {
using namespace PAM3;
// Keep the legacy serialization identity to allow import of old SLOT/v1 saves.
// Never load the v2 DLL beside the v3 DLL in the same game instance.
constexpr std::uint32_t kSerializationID = FourCC('P', 'A', 'M', 'S');
constexpr std::uint32_t kSlotRecordType = FourCC('S', 'L', 'O', 'T');
constexpr std::uint32_t kRecordVersion = 2;

PersistedState g_state;
std::mutex g_stateMutex;
std::atomic_bool g_transactionActive = false;
// Preserve unparseable saved bytes across subsequent saves instead of
// overwriting the player's only copy with an empty registry.
std::vector<std::uint8_t> g_quarantineBytes;
std::uint32_t g_quarantineVersion = 0;
std::atomic_bool g_quarantined = false;
int g_loadHealth = 0; // 0=new, 1=legacy v1, 2=v2, -1=quarantine.

void AuditLog(const char* message) {
    std::ofstream out("Data/F4SE/Plugins/PowerArmorMgr3-core.log", std::ios::app);
    if (out) out << message << '\n';
}

class Transaction {
public:
    Transaction() : acquired_(!g_transactionActive.exchange(true)) {}
    ~Transaction() { if (acquired_) g_transactionActive.store(false); }
    explicit operator bool() const { return acquired_; }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
private:
    bool acquired_;
};


std::string ArmorName(RE::TESObjectREFR* frame) {
    if (!frame) return "Power Armor";
    auto* name = frame->GetDisplayFullName();
    if (!name || !name[0]) return "Power Armor";
    std::string result(name);
    if (result.size() > 120) result.resize(120);
    return result;
}

std::string LowerAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool IsPowerArmorForm(RE::TESForm* form) {
    if (!form) return false;
    if (auto* armor = form->As<RE::TESObjectARMO>()) {
        if (armor->HasKeywordString("ArmorTypePower") ||
            armor->HasKeywordString("PowerArmorBodyPart") ||
            armor->HasKeywordString("PowerArmorFrame")) return true;
    }
    // Preserve the original fallback for bare chassis and third-party frames.
    const std::string_view display = RE::TESFullName::GetFullName(*form);
    if (display.empty()) return false;
    const auto name = LowerAscii(std::string(display));
    return name.find("power armor") != std::string::npos ||
        name.find("chassis") != std::string::npos ||
        name.find("frame") != std::string::npos;
}

bool IsPowerArmorReference(RE::TESObjectREFR* frame) {
    if (!frame) return false;
    // Original 2.2.7 accepted both tagged frames and frames identified
    // through their armor inventory. Preserve that gameplay contract.
    if (IsPowerArmorForm(frame->GetObjectReference())) return true;
    if (!frame->inventoryList) return false;
    for (auto& item : frame->inventoryList->data) {
        if (IsPowerArmorForm(item.object)) return true;
    }
    return false;
}

RE::TESObjectREFR* FindFrame(std::uint32_t id) {
    if (!id) return nullptr;
    auto* form = RE::TESForm::GetFormByID(id);
    return form ? form->As<RE::TESObjectREFR>() : nullptr;
}

bool HandleMatchesReference(RE::ObjectRefHandle handle, const RE::TESObjectREFR* target) {
    if (!handle || !target) return false;
    auto ref = handle.get();
    return ref && ref->GetFormID() == target->GetFormID();
}

bool IsOccupied(RE::TESObjectREFR* frame) {
    auto* cell = frame ? frame->GetParentCell() : nullptr;
    if (!cell) return false;
    bool occupied = false;
    cell->ForEachReferenceInRange(frame->GetPosition(), 512.0F, [&](RE::TESObjectREFR* ref) {
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor || !actor->currentProcess) return RE::BSContainer::ForEachResult::kContinue;
        if (HandleMatchesReference(actor->currentProcess->GetOccupiedFurniture(), frame)) {
            occupied = true;
            return RE::BSContainer::ForEachResult::kStop;
        }
        if (auto* high = actor->currentProcess->middleHigh;
            high && (HandleMatchesReference(high->currentFurniture, frame) ||
                     HandleMatchesReference(high->occupiedFurniture, frame))) {
            occupied = true;
            return RE::BSContainer::ForEachResult::kStop;
        }
        return RE::BSContainer::ForEachResult::kContinue;
    });
    return occupied;
}

bool IsPlayerOwnedOrUnowned(RE::TESObjectREFR* frame) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!frame || !player) return false;
    if (!frame->GetHasOwner() && frame->GetOwner() == nullptr) return true;
    return frame->IsAnOwner(player, true, true);
}

// Keep the known 2.2.7 placement behavior, but NEVER remove the persisted
// slot record unless the target is verified enabled in the destination cell.
bool MovePowerArmorToPlayer(RE::TESObjectREFR* frame, RE::Actor* player) {
    if (!frame || !player || !player->parentCell) return false;
    auto* cell = player->parentCell;
    if (!frame->IsDisabled()) frame->Disable();
    if (frame->parentCell != cell) {
        frame->MoveRefToNewSpace(cell, cell->IsExterior() ? cell->worldSpace : nullptr);
    }
    const float angle = player->data.angle.z;
    RE::NiPoint3 loc = player->data.location;
    loc.x += std::sin(angle) * 190.0F;
    loc.y += std::cos(angle) * 190.0F;
    loc.z += 12.0F;
    frame->SetLocationOnReference(loc);
    frame->SetAngleOnReference(RE::NiPoint3{ 0.0F, 0.0F, angle });
    if (frame->parentCell != cell) {
        frame->Disable();
        return false;
    }
    const auto actual = frame->GetPosition();
    const float distanceSquared =
        std::pow(actual.x - loc.x, 2.0F) +
        std::pow(actual.y - loc.y, 2.0F) +
        std::pow(actual.z - loc.z, 2.0F);
    if (distanceSquared > 4096.0F) {
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

// Result codes:
//   1 success; 0 unresolved/unavailable; negatives are explicit failures.
//  -1 invalid argument, -2 occupied slot, -3 in use, -4 ownership,
//  -5 combat, -6 duplicate reference, -7 primary not bound,
//  -8 transaction in progress, -9 world-state conflict,
// -10 engine transition failed, -11 unresolved serialized ID,
// -12 existing primary binding protects slot 01; -13 quarantined save.
int StorePowerArmor(int slot, RE::TESObjectREFR* frame) {
    if (g_quarantined.load()) return -13;
    Transaction tx;
    if (!tx) return -8;
    if (!InRange(slot) || !frame || !IsPowerArmorReference(frame)) return -1;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || player->IsInCombat()) return -5;
    if (IsOccupied(frame)) return -3;
    if (!IsPlayerOwnedOrUnowned(frame)) return -4;
    if (frame->IsDisabled()) return -9;
    const auto formID = frame->GetFormID();
    if (formID == 0) return -1;
    {
        std::lock_guard lock(g_stateMutex);
        if (g_state.slots.contains(slot)) return -2;
        for (const auto& [storedSlot, record] : g_state.slots) {
            if (!record.unresolvedID && record.frameID == formID) return -6;
        }
        if (slot == 1 && g_state.primary.frameID != 0 &&
            (g_state.primary.unresolvedID || g_state.primary.frameID != formID)) {
            return -12;
        }
    }
    const ArmorSlot record{ formID, ArmorName(frame), false };
    frame->Disable();
    if (!frame->IsDisabled()) return -10;
    {
        std::lock_guard lock(g_stateMutex);
        g_state.slots.emplace(slot, record);
        if (slot == 1) g_state.primary = { formID, false };
    }
    return 1;
}

int DeployPowerArmor(int slot, RE::Actor* player) {
    if (g_quarantined.load()) return -13;
    Transaction tx;
    if (!tx) return -8;
    if (!InRange(slot) || !player) return -1;
    if (player->IsInCombat()) return -5;
    ArmorSlot record;
    {
        std::lock_guard lock(g_stateMutex);
        const auto it = g_state.slots.find(slot);
        if (it == g_state.slots.end()) return -1;
        record = it->second;
    }
    if (record.unresolvedID) return -11;
    auto* frame = FindFrame(record.frameID);
    if (!frame || !IsPowerArmorReference(frame)) return 0;
    if (!frame->IsDisabled()) return -9;
    if (!MovePowerArmorToPlayer(frame, player)) return -10;
    {
        std::lock_guard lock(g_stateMutex);
        g_state.slots.erase(slot);  // Commit only after engine verification.
        // The 01 primary link remains after deployment for hotkey recall.
    }
    return 1;
}

int SlotStatus(int slot) {
    if (g_quarantined.load()) return -13;
    if (!InRange(slot)) return -1;
    ArmorSlot record;
    {
        std::lock_guard lock(g_stateMutex);
        auto it = g_state.slots.find(slot);
        if (it == g_state.slots.end()) return 0;
        record = it->second;
    }
    if (record.unresolvedID) return 2; // Cannot map saved FormID.
    auto* frame = FindFrame(record.frameID);
    if (!frame || !IsPowerArmorReference(frame)) return 2; // Do not erase.
    return frame->IsDisabled() ? 1 : 3; // Unexpectedly already in world.
}

int TogglePrimary() {
    if (g_quarantined.load()) return -13;
    if (SlotStatus(1) != 0) {
        return DeployPowerArmor(1, RE::PlayerCharacter::GetSingleton());
    }
    PrimaryLink link;
    {
        std::lock_guard lock(g_stateMutex);
        link = g_state.primary;
    }
    if (!link.frameID) return -7;
    if (link.unresolvedID) return -11;
    auto* frame = FindFrame(link.frameID);
    if (!frame || !IsPowerArmorReference(frame)) return 0;
    return StorePowerArmor(1, frame);
}

bool ClearPrimaryBinding() {
    if (g_transactionActive.load() || g_quarantined.load()) return false;
    std::lock_guard lock(g_stateMutex);
    if (g_state.slots.contains(1)) return false;
    g_state.primary = {};
    return true;
}

int CurrentSlot() {
    std::lock_guard lock(g_stateMutex);
    return g_state.currentSlot;
}
void SetCurrentSlot(int slot) {
    if (!InRange(slot)) return;
    std::lock_guard lock(g_stateMutex);
    g_state.currentSlot = slot;
}
int OccupiedCount() {
    std::lock_guard lock(g_stateMutex);
    return static_cast<int>(g_state.slots.size());
}
int GetLoadHealth() {
    std::lock_guard lock(g_stateMutex);
    return g_loadHealth;
}

// Strict, self-checking v2 record codec. Corrupt records do not partially
// mutate the currently loaded registry.
void ResolveLoadedIDs(const F4SE::SerializationInterface* ser, PersistedState& state) {
    auto remap = [ser](std::uint32_t& formID, bool& unresolved) {
        if (formID == 0 || unresolved) return;
        if (auto resolved = ser->ResolveFormID(formID); resolved && *resolved != 0) {
            formID = *resolved;
        } else {
            unresolved = true; // Preserve raw old ID for diagnostics only.
        }
    };
    for (auto& [slot, record] : state.slots) {
        remap(record.frameID, record.unresolvedID);
    }
    remap(state.primary.frameID, state.primary.unresolvedID);
}

void SkipRecord(const F4SE::SerializationInterface* ser, std::uint32_t length) {
    std::array<std::uint8_t, 1024> scratch{};
    while (length > 0) {
        const auto amount = std::min<std::uint32_t>(length, static_cast<std::uint32_t>(scratch.size()));
        if (ser->ReadRecordData(scratch.data(), amount) != amount) break;
        length -= amount;
    }
}

void F4SEAPI OnSave(const F4SE::SerializationInterface* ser) {
    PersistedState snapshot;
    std::vector<std::uint8_t> quarantined;
    std::uint32_t originalVersion = 0;
    {
        std::lock_guard lock(g_stateMutex);
        snapshot = g_state;
        if (g_quarantined.load()) {
            quarantined = g_quarantineBytes;
            originalVersion = g_quarantineVersion;
        }
    }
    if (g_quarantined.load()) {
        if (quarantined.empty()) {
            AuditLog("Cannot preserve a truncated/oversized corrupt record; do NOT overwrite original save.");
            return;
        }
        // Byte-for-byte roundtrip of corrupt/unknown future records.
        if (!ser->OpenRecord(kSlotRecordType, originalVersion) ||
            !ser->WriteRecordData(quarantined.data(), static_cast<std::uint32_t>(quarantined.size())))
            AuditLog("Failed to preserve quarantined serialization bytes.");
        return;
    }
    const auto bytes = EncodeV2(snapshot);
    if (bytes.size() > kMaxRecordBytes || !ser->OpenRecord(kSlotRecordType, kRecordVersion)) return;
    if (!ser->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size())))
        AuditLog("Failed to write storage snapshot.");
}

void F4SEAPI OnRevert(const F4SE::SerializationInterface*) {
    std::lock_guard lock(g_stateMutex);
    g_state = {};
    g_quarantineBytes.clear();
    g_quarantineVersion = 0;
    g_quarantined.store(false);
    g_loadHealth = 0;
    g_transactionActive.store(false);
}

void F4SEAPI OnLoad(const F4SE::SerializationInterface* ser) {
    OnRevert(ser);
    std::uint32_t type = 0, version = 0, length = 0;
    while (ser->GetNextRecordInfo(type, version, length)) {
        if (type != kSlotRecordType) {
            SkipRecord(ser, length);
            continue;
        }
        if (length > kMaxRecordBytes) {
            SkipRecord(ser, length);
            AuditLog("Oversized storage record: failed closed. Do not resave the affected game.");
            std::lock_guard lock(g_stateMutex);
            g_quarantined.store(true);
            g_loadHealth = -1;
            continue;
        }
        std::vector<std::uint8_t> bytes(length);
        if (ser->ReadRecordData(bytes.data(), length) != length) {
            AuditLog("Truncated storage record: failed closed.");
            std::lock_guard lock(g_stateMutex);
            g_quarantined.store(true);
            g_loadHealth = -1;
            continue;
        }
        PersistedState loaded;
        const bool supported = version == 1 || version == kRecordVersion;
        const bool valid = supported &&
            (version == 1 ? DecodeLegacyV1(bytes, loaded) : DecodeV2(bytes, loaded));
        if (!valid) {
            AuditLog("Unrecognized or damaged storage record quarantined. Original bytes retained.");
            std::lock_guard lock(g_stateMutex);
            g_quarantineBytes = std::move(bytes);
            g_quarantineVersion = version;
            g_quarantined.store(true);
            g_loadHealth = -1;
            continue;
        }
        ResolveLoadedIDs(ser, loaded);
        std::lock_guard lock(g_stateMutex);
        if (!g_quarantined.load()) {
            g_state = std::move(loaded);
            g_loadHealth = version == 1 ? 1 : 2;
        }
    }
}

int NativeStore(std::monostate, int slot, RE::TESObjectREFR* frame) { return StorePowerArmor(slot, frame); }
int NativeDeploy(std::monostate, int slot, RE::Actor* player) { return DeployPowerArmor(slot, player); }
int NativeTogglePrimary(std::monostate) { return TogglePrimary(); }
int NativeStatus(std::monostate, int slot) { return SlotStatus(slot); }
int NativeCount(std::monostate) { return OccupiedCount(); }
int NativeLoadHealth(std::monostate) { return GetLoadHealth(); }
int NativeGetCurrentSlot(std::monostate) { return CurrentSlot(); }
void NativeSetCurrentSlot(std::monostate, int slot) { SetCurrentSlot(slot); }
bool NativeClearPrimary(std::monostate) { return ClearPrimaryBinding(); }

bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm) {
    if (!vm) return false;
    vm->BindNativeMethod("PAM3Native", "StorePowerArmor", NativeStore);
    vm->BindNativeMethod("PAM3Native", "DeployPowerArmor", NativeDeploy);
    vm->BindNativeMethod("PAM3Native", "TogglePrimary", NativeTogglePrimary);
    vm->BindNativeMethod("PAM3Native", "GetSlotStatus", NativeStatus);
    vm->BindNativeMethod("PAM3Native", "GetOccupiedCount", NativeCount);
    vm->BindNativeMethod("PAM3Native", "GetLoadHealth", NativeLoadHealth);
    vm->BindNativeMethod("PAM3Native", "GetCurrentSlot", NativeGetCurrentSlot);
    vm->BindNativeMethod("PAM3Native", "SetCurrentSlot", NativeSetCurrentSlot);
    vm->BindNativeMethod("PAM3Native", "ClearPrimaryBinding", NativeClearPrimary);
    return true;
}

} // namespace

extern "C" [[maybe_unused]] __declspec(dllexport)
F4SE::PluginVersionData F4SEPlugin_Version = []() noexcept {
    F4SE::PluginVersionData v{};
    v.PluginVersion({ 3, 0, 0, 0 });
    v.PluginName("PowerArmorMgr");
    v.AuthorName("AozoraCynthiAStudio");
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

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* interface) {
    F4SE::Init(interface, { .log = false, .hook = false });
    if (auto* papyrus = F4SE::GetPapyrusInterface()) papyrus->Register(RegisterPapyrus);
    if (auto* ser = F4SE::GetSerializationInterface()) {
        ser->SetUniqueID(kSerializationID);
        ser->SetSaveCallback(OnSave);
        ser->SetLoadCallback(OnLoad);
        ser->SetRevertCallback(OnRevert);
    }
    return true;
}

extern "C" [[maybe_unused]] __declspec(dllexport)
bool F4SEPlugin_Query(const F4SE::QueryInterface* interface, F4SE::PluginInfo* info) {
    info->infoVersion = F4SE::PluginInfo::kVersion;
    info->name = "PowerArmorMgr";
    info->version = 3;
    if (interface->IsEditor()) return false;
    return interface->RuntimeVersion() >= F4SE::RUNTIME_1_10_162;
}
