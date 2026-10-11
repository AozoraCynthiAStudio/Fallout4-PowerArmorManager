#include <cassert>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>
#include "../native/src/SlotCodec.h"

using namespace PAM3;

static std::vector<std::uint8_t> MakeLegacyV1() {
    std::vector<std::uint8_t> out;
    Add32(out, 23); // Selected bay.
    Add32(out, 2);  // Count.
    Add32(out, 1);  Add32(out, 0); Add32(out, 0xFF123456);
    AddString(out, "Legacy Primary");
    Add32(out, 1); // One installed part in the legacy representation.
    Add32(out, 0x00001234); AddString(out, "T-60 Torso");
    Add32(out, 50); Add32(out, 0); Add32(out, 0x01005678);
    AddString(out, "Legacy Reserve");
    Add32(out, 0); // Frame with no recorded pieces is still recoverable.
    return out;
}

int main() {
    // Deterministic v2 roundtrip including linked, missing and ordinary bays.
    PersistedState original;
    original.currentSlot = 25;
    original.primary = { 0xFF123456, false };
    original.slots.emplace(1, ArmorSlot{ 0xFF123456, "Primary", false });
    original.slots.emplace(12, ArmorSlot{ 0x00004567, "Backup", false });
    original.slots.emplace(50, ArmorSlot{ 0xFEAB1234, "Unresolved - keep", true });
    const auto encoded = EncodeV2(original);
    assert(encoded == EncodeV2(original));
    PersistedState loaded;
    assert(DecodeV2(encoded, loaded));
    assert(loaded.currentSlot == 25);
    assert(loaded.primary.frameID == original.primary.frameID);
    assert(loaded.slots.size() == 3);
    assert(loaded.slots.at(50).unresolvedID);
    assert(loaded.slots.at(12).name == "Backup");

    // CRC must prevent partially imported records.
    auto tampered = encoded;
    tampered[24] ^= 0x40;
    PersistedState sentinel;
    sentinel.currentSlot = 17;
    assert(!DecodeV2(tampered, sentinel));
    assert(sentinel.currentSlot == 17);
    assert(sentinel.slots.empty());

    for (std::size_t size = 0; size < encoded.size(); ++size) {
        assert(!DecodeV2(std::span<const std::uint8_t>(encoded.data(), size), sentinel));
    }

    // Legacy 2.2.7 source format: preserve frames, not the old Outfit logic.
    PersistedState legacy;
    const auto v1 = MakeLegacyV1();
    assert(DecodeLegacyV1(v1, legacy));
    assert(legacy.currentSlot == 23);
    assert(legacy.slots.size() == 2);
    assert(legacy.primary.frameID == 0xFF123456);
    assert(legacy.slots.at(50).name == "Legacy Reserve");

    auto truncatedV1 = v1;
    truncatedV1.pop_back();
    assert(!DecodeLegacyV1(truncatedV1, sentinel));
    assert(sentinel.currentSlot == 17);

    // Invalid counts and duplicate slots must be rejected.
    std::vector<std::uint8_t> overCount;
    Add32(overCount, 1);
    Add32(overCount, 51);
    assert(!DecodeLegacyV1(overCount, sentinel));

    auto duplicated = v1;
    // Second slot starts after the first frame and one part.
    // Build an explicit duplicate fixture rather than assuming offsets.
    duplicated.clear();
    Add32(duplicated, 1); Add32(duplicated, 2);
    for (int i = 0; i < 2; ++i) {
        Add32(duplicated, 1); Add32(duplicated, 0); Add32(duplicated, i + 100);
        AddString(duplicated, "duplicate");
        Add32(duplicated, 0);
    }
    assert(!DecodeLegacyV1(duplicated, sentinel));

    // Empty state is a valid v2 snapshot.
    PersistedState empty;
    auto emptyBytes = EncodeV2(empty);
    PersistedState restored;
    assert(DecodeV2(emptyBytes, restored));
    assert(restored.slots.empty());

    std::cout << "PASS: v2 CRC and roundtrip; legacy v1; truncation; malformed data\n";
    return 0;
}
