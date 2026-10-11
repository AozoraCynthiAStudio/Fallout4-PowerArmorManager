# Power Armor Manager 3.0 — Core-first rewrite (EXPERIMENTAL)

## Status and safety

This is a new **backend-only development implementation**, NOT a playable 3.0 release.
It is isolated in Source/rebuild on branch rebuild/3.0-core-first.
The working 2.2.7 source and release packages are untouched.
Do not upload this as a mod or use it on irreplaceable saves yet.

Goals:
- An actual power-armor-only core, with no Outfit Manager legacy.
- Engine-side store / deploy transactions; the occupied record is never erased on failed deployment.
- No PrismaUI, SWF, menu thread, 50 ms poller, mouse focus or layout calibrator.
- 50 bays and one persistent Primary Armor link.
- Deliberately plain MCM keyboard/gamepad hotkeys to verify behavior before UI design.
- Versioned F4SE save codec, CRC32, bounded parsing, legacy SLOT/v1 importer and quarantine.
- Clean API and portable C++23 binary serialization tests.

## Source

- native/src/SlotCodec.h: engine-independent slot model; v2 codec and legacy-v1 codec.
- native/src/main.cpp: F4SE runtime registration, serial load/save, frame validation, store/deploy/primary.
- Scripts/Source/User/PAM3Native.psc: 9-method Papyrus native contract.
- Scripts/Source/User/PAM3Hotkeys.psc: player test actions and explicit messages.
- MCM/Config/PowerArmorMgr: temporary test-hotkey configuration.
- tests: real portable C++ codec tests and static MCM/Papyrus bridge tests.

Use the original PowerArmorMgr.esp only in a controlled local test setup.
The old 2.2.7 DLL and the new 3.0 DLL have the same module name and serialization
unique ID, so NEVER install both or merge their files in a single deployment.
Compiled PEX and DLL are NOT provided here; the source requires building.
Use a COPY of a test save. Never overwrite your only 2.2.7 save.

## Build (not yet validated on Windows)

Requires MSVC x64, xmake 3.x, compatible CommonLibF4 and Papyrus compiler.

  set COMMONLIBF4_XMAKE_ROOT to the matching CommonLibF4 xmake root
  cd Source/rebuild/native
  build_dll.bat

Copy scripts to a test-only staging directory and compile PAM3Native.psc and
PAM3Hotkeys.psc with Fallout 4's Papyrus compiler and required source imports.
Do not package until integration tests pass. GOE is needed for target selection
in the temporary MCM test hotkey, but native deployment itself does not use GOE.

Run portable checks from the repository root:

  g++ -std=c++23 -Wall -Wextra -Werror -pedantic \
      Source/rebuild/tests/test_slot_codec.cpp -o codec_test
  ./codec_test
  python3 -m unittest discover -s Source/rebuild/tests -p 'test_*.py' -v

## Runtime semantics

Store requires a target that is recognizably a power-armor frame, empty bay,
not occupied, unowned/player-owned, player out of combat, and enabled in-world.
It disables the same reference rather than destroying/replacing its pieces,
then records the specific reference ID. No equipment copying or duplicate suit
generation occurs.

Deploy resolves the exact stored reference, moves it, verifies the destination
cell and position, enables and refreshes 3D, and ONLY THEN removes the bay
record. If resolution or movement fails, the bay remains recorded.

Primary: bay 01 store binds that exact frame; deploy leaves binding active;
Primary hotkey recall attempts to store that same frame in bay 01.
Rebinding to a different frame requires explicit ClearPrimaryBinding while
bay 01 is empty. This command is intentionally not assigned a hotkey.

No durability UI or other visual feature is implemented in this phase.

## Slot status

0 empty; 1 occupied and exact frame found disabled; 2 occupied but exact frame
not currently resolvable; 3 occupied with unexpected world-enabled state;
-13 F4SE record invalid/quarantined, block all actions.

## Return codes

1 success; 0 frame reference cannot currently be found.
-1 invalid; -2 occupied; -3 player/NPC using frame;
-4 ownership; -5 combat; -6 duplicate; -7 primary unbound;
-8 operation busy; -9 world state conflict; -10 engine transition failed;
-11 save FormID could not be safely remapped;
-12 primary still linked to a different suit; -13 unreadable save quarantined.

## Save compatibility

The F4SE unique ID stays PAMS and record type SLOT.
New records use v2: magic PAM3, selected slot, primary form ID plus status,
up to 50 records, each form ID + name + unresolved flag, and CRC32.
Unknown, invalid and damaged records are quarantined; their raw bytes are
preserved on subsequent serialization when complete and within size cap.
No C++ method may act on a quarantined save.

A legacy SLOT/v1 decoder exists for the schema visible in the repository
main.cpp. The RELEASED 2.2.7 DLL may differ from that source; in-game
migration compatibility is NOT yet established. Legacy-only deployed
Primary binding cannot be reconstructed from that source v1 data.

Save changes and an old object reference disappearing from the game are
different failure cases: CRC and quarantine protect the first, but cannot
resurrect a world object already deleted by the engine or another mod.
Dynamic reference persistence, F4SE remap behavior and multi-save rollover
MUST be tested before release.

## Release blockers

- Windows CommonLibF4/xmake native compilation and Papyrus compilation.
- Actual Fallout 4 save/reload/in-game deployment testing across cells.
- Proof that old 2.2.7 release ZIP serialized bytes match the importer.
- Investigate transient/dynamic form reference lifetime under reload.
- Record deletion/ownership conflict recovery policy and user diagnostics.
- New ESP packaging/compatibility decision.
- Real SWF UI only AFTER the core is proven.

The word "experimental" remains until all items above are checked.
