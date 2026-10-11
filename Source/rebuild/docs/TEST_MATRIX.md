# In-game test matrix — Power Armor Manager 3.0

Keep an untouched backup of every test save. Never overwrite the only copy.
Recording PASS means observed in actual Fallout 4, not a unit test.

| Case | Scenario | Expected invariant | Status |
|---|---|---|---|
| ST-01 | Store empty vanilla chassis | Same frame disabled, bay=1 | NOT RUN |
| ST-02 | Store fully modified suit | Inventory/paint/perks preserved when deployed | NOT RUN |
| ST-03 | Store modded or DLC armor | Frame identity and equipment preserved | NOT RUN |
| ST-04 | Occupied frame (player/NPC) | Reject, no mutation | NOT RUN |
| ST-05 | Owned frame and combat | Reject, no mutation | NOT RUN |
| ST-06 | Duplicate suit / used bay | Reject, no overwrite | NOT RUN |
| ST-07 | Deploy from same cell | Same reference enabled at player | NOT RUN |
| ST-08 | Deploy interior → exterior | Same reference, exactly one frame | NOT RUN |
| ST-09 | Deploy exterior → interior | Same reference, exactly one frame | NOT RUN |
| ST-10 | Force placement failure | Bay remains, recovery/retry possible | NOT RUN |
| SV-01 | Store, manual save, exit, reload | Bay still 1, correct frame present | NOT RUN |
| SV-02 | Store, quicksave, exit, reload | Same as SV-01 | NOT RUN |
| SV-03 | Store, fast travel, save/load | Same stored identity | NOT RUN |
| SV-04 | 20+ sequential save/load cycles | No lost/duplicated suits | NOT RUN |
| SV-05 | Switch between two characters' saves | Independent inventories/slot lists | NOT RUN |
| SV-06 | Populate all 50 bays, save/reload | Every bay distinct and recoverable | NOT RUN |
| SV-07 | Invalid/unresolved FormID | Bay not cleared; no unrelated frame touched | NOT RUN |
| SV-08 | Corrupt v2 CRC test fixture | Quarantine, no actions; raw record retained | NOT RUN |
| SV-09 | Import real 2.2.7 release save | No missing/duplicated legacy frames | NOT RUN |
| PR-01 | Primary stored → hotkey deploy | Binding persists after deployment | NOT RUN |
| PR-02 | Primary deployed → hotkey recall | Exact same suit stored in bay 01 | NOT RUN |
| PR-03 | Primary deployed, save/reload, recall | Same exact reference recognized | NOT RUN |
| PR-04 | Attempt primary rebind without clear | Rejected | NOT RUN |
| UX-01 | MCM keyboard and gamepad hotkeys | Reliable independent of any UI | NOT RUN |
| UX-02 | Missing PrismaUI entirely | Core still stores and deploys | NOT RUN |
| UX-03 | Missing GOE | Deploy core works; target selection unavailable | NOT RUN |

Portable C++ serializer tests and Python contract tests are separate CI checks;
they DO NOT count as passing a single in-game case above.

Bug reporting must include Fallout runtime version, F4SE version, installed
mods/load order, original slot, operation, before/after save, status code,
and whether frame was spawned (FF FormID) or plugin-defined.
No test may silently treat a missing reference as an empty bay.
