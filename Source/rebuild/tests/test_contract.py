import json
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NativePapyrusContract(unittest.TestCase):
    def test_all_native_methods_are_registered(self):
        psc = (ROOT / "Scripts/Source/User/PAM3Native.psc").read_text(encoding="utf-8")
        cpp = (ROOT / "native/src/main.cpp").read_text(encoding="utf-8")
        declared = set(re.findall(r"\bFunction\s+(\w+)\([^)]*\)\s+Global\s+Native", psc))
        bound = set(re.findall(r'BindNativeMethod\("PAM3Native",\s*"(\w+)"', cpp))
        self.assertEqual(declared, bound)
        self.assertEqual(len(declared), 9)

    def test_mcm_actions_exist_in_papyrus(self):
        config = json.loads((ROOT / "MCM/Config/PowerArmorMgr/config.json").read_text(encoding="utf-8"))
        binds = json.loads((ROOT / "MCM/Config/PowerArmorMgr/keybinds.json").read_text(encoding="utf-8"))
        script = (ROOT / "Scripts/Source/User/PAM3Hotkeys.psc").read_text(encoding="utf-8")
        config_keys = {
            item["id"]
            for page in config["pages"]
            for item in page["content"]
            if item["type"] == "hotkey"
        }
        binding_keys = {bind["id"] for bind in binds["keybinds"]}
        self.assertEqual(config_keys, binding_keys)
        for bind in binds["keybinds"]:
            self.assertEqual(bind["action"]["script"], "PAM3Hotkeys")
            name = bind["action"]["function"]
            self.assertRegex(script, rf"\bFunction\s+{name}\(")

    def test_core_is_ui_agnostic(self):
        cpp = (ROOT / "native/src/main.cpp").read_text(encoding="utf-8")
        self.assertNotIn('#include "PrismaUI_F4_API.h"', cpp)
        self.assertNotIn("CreateView(", cpp)
        self.assertNotIn("std::thread", cpp)
        self.assertNotIn("SaveOutfit(", cpp)
        self.assertIn("ResolveLoadedIDs", cpp)
        self.assertIn("if (record.unresolvedID) return -11", cpp)

    def test_no_destructive_clear_on_failed_deploy(self):
        cpp = (ROOT / "native/src/main.cpp").read_text(encoding="utf-8")
        assert cpp.index("if (!MovePowerArmorToPlayer(frame, player)) return -10;") < cpp.index(
            "g_state.slots.erase(slot);"
        )


if __name__ == "__main__":
    unittest.main()
