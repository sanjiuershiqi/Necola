#pragma once
// ---------------------------------------------------------------------------
// L4N (Left 4 Neko) environment awareness layer.
//
// Purpose: make the plugin L4N-aware instead of L4N-agnostic.
//  - Stores the HMODULE handles L4N hands us via IL4NPlugin::OnModuleLoaded
//    using the OFFICIAL conversion pattern from l4n_plugin.h:
//        std::bit_cast<HMODULE>(handle)
//    (module_name arrives WITHOUT the ".dll" suffix, e.g. "client").
//  - Probes L4N's presence through its registered cvars once ICvar is up.
//  - Exposes the handful of L4N cvars we must COORDINATE with:
//      l4n_game_hud_visible          master HUD switch (crosshair logic)
//      l4n_patch_hud_scope           L4N takes over sniper-scope HUD
//      l4n_vm_sway                   viewmodel sway (ADS anim feel)
//      l4n_vm_sway_ignore_helpinghand may strip sway from plugin ADS anims
//
// All accessors degrade gracefully on a vanilla (non-L4N) engine.
// ---------------------------------------------------------------------------

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

class ConVar;

namespace L4N {

class CEnv {
public:
    // ---- module tracking (call from IL4NPlugin::OnModuleLoaded) ----------
    // Official pattern: handle is the LoadLibrary HMODULE, converted with
    // std::bit_cast<HMODULE>(handle). Safe to call from L4N's thread.
    void OnModuleLoaded(const char* name, std::uintptr_t handle);

    // Lookup by either "client" or "client.dll" (case-insensitive).
    // Falls back to GetModuleHandleA so the layer also works when L4N
    // never reported the module (e.g. manual injection debugging).
    HMODULE FindModule(const char* name) const;

    size_t ModuleCount() const;

    // ---- runtime probe (call once I::Cvars is populated) -----------------
    // Detects L4N, caches coordinated cvars and emits a one-shot
    // coordination/conflict report to the diagnostic log.
    void Init();

    bool Detected() const { return m_detected; }

    // ---- coordinated L4N state (per-frame safe) ---------------------------
    // l4n_game_hud_visible — when L4N hides the whole HUD, our crosshair
    // logic must stand down instead of fighting the platform.
    // Default true (vanilla engine / cvar absent).
    bool HudVisible() const;

    // l4n_patch_hud_scope — L4N renders the sniper-scope HUD itself.
    // Default false.
    bool PatchHudScope() const;

    // l4n_vm_sway — viewmodel sway master switch. Default true.
    bool SwayEnabled() const;

    // l4n_vm_sway_ignore_helpinghand — official readme notes this can strip
    // sway from plugin ADS animations. Default false.
    bool SwayIgnoreHelpingHand() const;

private:
    // Cached accessor; retries ICvar::FindVar until the cvar shows up so
    // late registration (after our Init) is still picked up.
    ConVar* Cvar(const char* name, ConVar** cache) const;

    static bool ReadBool(const ConVar* v, bool def);

    static std::string NormalizeKey(const char* name);

    mutable std::mutex m_moduleMtx;  // guards m_modules only
    std::unordered_map<std::string, HMODULE> m_modules;

    bool m_detected = false;

    // Lazily-resolved cvar pointers. Written from at most two threads
    // (init thread + paint thread) with identical values — benign race,
    // aligned pointer stores on x86.
    mutable ConVar* m_cvHudVisible = nullptr;
    mutable ConVar* m_cvPatchHudScope = nullptr;
    mutable ConVar* m_cvSway = nullptr;
    mutable ConVar* m_cvSwayIgnoreHelpingHand = nullptr;
};

inline CEnv Env;

} // namespace L4N
