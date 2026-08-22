set_xmakever("3.0.0")

set_project("PowerArmorMgr")
set_version("1.0.0")
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

add_rules("mode.debug", "mode.releasedbg")

local commonlib_root = os.getenv("COMMONLIBF4_XMAKE_ROOT")
if commonlib_root == nil or commonlib_root == "" then
    raise("Set COMMONLIBF4_XMAKE_ROOT to the CommonLibF4 xmake root before configuring this project.")
end
includes(commonlib_root)

target("PowerArmorMgr", function()
    add_rules("commonlibf4.plugin", {
        name = "PowerArmorMgr",
        author = "PowerArmorMgr Author",
        plugin_template = "commonlibf4-plugin.cpp.in"
    })

    add_files("src/**.cpp")
    add_includedirs("src")
end)
