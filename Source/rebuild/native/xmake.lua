set_xmakever("3.0.0")
set_project("PowerArmorMgr")
set_version("3.0.0")
set_arch("x64")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")
add_rules("mode.debug", "mode.releasedbg")

local root = os.getenv("COMMONLIBF4_XMAKE_ROOT")
if root == nil or root == "" then
    raise("Set COMMONLIBF4_XMAKE_ROOT before compiling.")
end
includes(root)

target("PowerArmorMgr", function()
    add_rules("commonlibf4.plugin", {
        name = "PowerArmorMgr",
        author = "AozoraCynthiAStudio",
        plugin_template = "commonlibf4-plugin.cpp.in"
    })
    add_files("src/main.cpp")
    add_includedirs("src")
end)
