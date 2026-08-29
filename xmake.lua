-- add_requires("microsoft-proxy 2.4.0")
add_rules("plugin.compile_commands.autoupdate")
add_rules("mode.debug", "mode.release")
set_policy("package.requires_lock", true)

set_languages("cxx20", "c++20")

set_plat("windows")
set_arch("x86")  -- Use "x64" for 64-bit builds

add_requires("spdlog")
add_requires("minhook")
add_requires("vcpkg::inipp")

-- Necola-ADS as an L4N (Left 4 Neko) plugin.
-- Produces necola_ads.dll, to be placed in <left4dead2>/bin/neko/plugins/.
-- Loaded by L4N via GetL4NPluginInstance (see necola/l4n_plugin.h).
local name = "necola_ads"
target(name)
    set_kind("shared")
    add_files("necola/dllmain.cpp")
    add_files("necola/sdk/*.cpp")
    add_files("necola/sdk/l4d2/*.cpp")
    add_files("necola/sdk/l4d2/interfaces/Cvars.cpp")
    add_files("necola/hook/**.cpp")
    add_packages("spdlog", "minhook", "vcpkg::inipp")
    add_links("user32", "gdi32")
