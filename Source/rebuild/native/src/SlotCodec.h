#pragma once
// Pure C++23 slot record model, independent of F4SE and Fallout 4.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace PAM3 {
inline constexpr int kMaxSlots = 50;
inline constexpr std::uint32_t kMaxRecordBytes = 8u * 1024u * 1024u;
inline constexpr std::uint32_t kMaxNameBytes = 4096u;
inline constexpr std::uint32_t FourCC(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(a) |
        (static_cast<std::uint32_t>(b) << 8) |
        (static_cast<std::uint32_t>(c) << 16) |
        (static_cast<std::uint32_t>(d) << 24);
}
inline constexpr std::uint32_t kRecordMagic = FourCC('P', 'A', 'M', '3');
struct ArmorSlot {
    std::uint32_t frameID = 0;
    std::string name;
    bool unresolvedID = false;
};
struct PrimaryLink {
    std::uint32_t frameID = 0;
    bool unresolvedID = false;
};
struct PersistedState {
    int currentSlot = 1;
    std::map<int, ArmorSlot> slots;
    PrimaryLink primary;
};
inline bool InRange(int slot) { return slot >= 1 && slot <= kMaxSlots; }

inline void Add32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
inline bool Read32(std::span<const std::uint8_t> data, std::size_t& pos, std::uint32_t& value) {
    if (pos > data.size() || data.size() - pos < 4) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(data[pos++]) << (i * 8);
    return true;
}
inline void AddString(std::vector<std::uint8_t>& out, const std::string& value) {
    const auto length = static_cast<std::uint32_t>(std::min<std::size_t>(value.size(), kMaxNameBytes));
    Add32(out, length);
    out.insert(out.end(), value.begin(), value.begin() + length);
}
inline bool ReadString(std::span<const std::uint8_t> data, std::size_t& pos, std::string& value) {
    std::uint32_t length = 0;
    if (!Read32(data, pos, length) || length > kMaxNameBytes || length > data.size() - pos) return false;
    value.assign(reinterpret_cast<const char*>(data.data() + pos), length);
    pos += length;
    return true;
}
inline std::uint32_t Crc32(std::span<const std::uint8_t> bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int n = 0; n < 8; ++n)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
inline std::vector<std::uint8_t> EncodeV2(const PersistedState& state) {
    std::vector<std::uint8_t> out;
    Add32(out, kRecordMagic);
    Add32(out, static_cast<std::uint32_t>(state.currentSlot));
    Add32(out, state.primary.frameID);
    Add32(out, state.primary.unresolvedID ? 1 : 0);
    Add32(out, static_cast<std::uint32_t>(state.slots.size()));
    for (const auto& [slot, record] : state.slots) {
        Add32(out, static_cast<std::uint32_t>(slot));
        Add32(out, record.frameID);
        Add32(out, record.unresolvedID ? 1 : 0);
        AddString(out, record.name);
    }
    Add32(out, Crc32(out));
    return out;
}
inline bool DecodeV2(std::span<const std::uint8_t> data, PersistedState& state) {
    if (data.size() < 24 || data.size() > kMaxRecordBytes) return false;
    std::uint32_t storedCRC = 0;
    std::size_t tail = data.size() - 4;
    if (!Read32(data, tail, storedCRC) || storedCRC != Crc32(data.first(data.size() - 4))) return false;
    std::size_t pos = 0;
    std::uint32_t magic = 0, current = 0, primaryID = 0, primaryFlags = 0, count = 0;
    if (!Read32(data, pos, magic) || magic != kRecordMagic ||
        !Read32(data, pos, current) || !Read32(data, pos, primaryID) ||
        !Read32(data, pos, primaryFlags) || !Read32(data, pos, count) ||
        count > kMaxSlots || primaryFlags > 1) return false;
    PersistedState next;
    next.currentSlot = InRange(static_cast<int>(current)) ? static_cast<int>(current) : 1;
    next.primary = { primaryID, primaryFlags != 0 };
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t slot = 0, id = 0, flags = 0;
        std::string name;
        if (!Read32(data, pos, slot) || !Read32(data, pos, id) ||
            !Read32(data, pos, flags) || !ReadString(data, pos, name) ||
            !InRange(static_cast<int>(slot)) || !id || flags > 1 ||
            next.slots.contains(static_cast<int>(slot))) return false;
        next.slots.emplace(static_cast<int>(slot), ArmorSlot{ id, std::move(name), flags != 0 });
    }
    if (pos != data.size() - 4) return false;
    state = std::move(next);
    return true;
}
inline bool DecodeLegacyV1(std::span<const std::uint8_t> data, PersistedState& state) {
    std::size_t pos = 0;
    std::uint32_t current = 0, count = 0;
    if (!Read32(data, pos, current) || !Read32(data, pos, count) || count > kMaxSlots) return false;
    PersistedState next;
    next.currentSlot = InRange(static_cast<int>(current)) ? static_cast<int>(current) : 1;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t slot = 0, gender = 0, formID = 0, itemCount = 0;
        std::string frameName;
        if (!Read32(data, pos, slot) || !Read32(data, pos, gender) ||
            !Read32(data, pos, formID) || !ReadString(data, pos, frameName) ||
            !Read32(data, pos, itemCount) || itemCount > 64) return false;
        for (std::uint32_t item = 0; item < itemCount; ++item) {
            std::uint32_t itemID = 0;
            std::string itemName;
            if (!Read32(data, pos, itemID) || !ReadString(data, pos, itemName)) return false;
        }
        if (!InRange(static_cast<int>(slot)) || !formID ||
            next.slots.contains(static_cast<int>(slot))) return false;
        next.slots.emplace(static_cast<int>(slot), ArmorSlot{ formID, std::move(frameName), false });
    }
    if (pos != data.size()) return false;
    // Legacy saves have no persisted deployed-primary binding in this source.
    if (auto it = next.slots.find(1); it != next.slots.end()) {
        next.primary = { it->second.frameID, false };
    }
    state = std::move(next);
    return true;
}


} // namespace PAM3
