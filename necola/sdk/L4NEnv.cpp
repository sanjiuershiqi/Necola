#include "L4NEnv.h"

#include "l4d2/interfaces/IConVar.h"
#include "../diag.h"

#include <bit>
#include <cctype>
#include <cstdio>

namespace {
void Log(const char* msg) {
    NecolaDiagLog(msg);
}

} // namespace

namespace L4N {

std::string CEnv::NormalizeKey(const char* name) {
    std::string key(name);
    for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // L4N reports module names WITHOUT the ".dll" suffix ("client").
    // Accept both spellings by stripping ours too.
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".dll") == 0)
        key.resize(key.size() - 4);
    return key;
}

void CEnv::OnModuleLoaded(const char* name, std::uintptr_t handle) {
    if (!name || !handle) return;
    // Official conversion from the l4n_plugin.h sample.
    const HMODULE hMod = std::bit_cast<HMODULE>(handle);
    std::lock_guard lock(m_moduleMtx);
    m_modules[NormalizeKey(name)] = hMod;
}

HMODULE CEnv::FindModule(const char* name) const {
    if (!name) return nullptr;
    HMODULE h = nullptr;
    {
        std::lock_guard lock(m_moduleMtx);
        auto it = m_modules.find(NormalizeKey(name));
        if (it != m_modules.end()) h = it->second;
    }
    if (h) return h;
    // Fallback: not reported by L4N (or vanilla engine) — ask the loader.
    return ::GetModuleHandleA(name);
}

size_t CEnv::ModuleCount() const {
    std::lock_guard lock(m_moduleMtx);
    return m_modules.size();
}

ConVar* CEnv::Cvar(const char* name, ConVar** cache) const {
    std::lock_guard lock(m_cvarMtx);
    if (*cache) return *cache;
    if (!I::Cvars) return nullptr;
    *cache = I::Cvars->FindVar(name);
    return *cache;
}

bool CEnv::ReadBool(const ConVar* v, bool def) {
    if (!v) return def;
    const ConVar* root = v->m_pParent ? v->m_pParent : v;
    return root->m_nValue != 0;
}

bool CEnv::HudVisible() const {
    return ReadBool(Cvar("l4n_game_hud_visible", &m_cvHudVisible), true);
}

bool CEnv::PatchHudScope() const {
    return ReadBool(Cvar("l4n_patch_hud_scope", &m_cvPatchHudScope), false);
}

bool CEnv::SwayEnabled() const {
    return ReadBool(Cvar("l4n_vm_sway", &m_cvSway), true);
}

bool CEnv::SwayIgnoreHelpingHand() const {
    return ReadBool(Cvar("l4n_vm_sway_ignore_helpinghand", &m_cvSwayIgnoreHelpingHand), false);
}

void CEnv::Init() {
    char buf[512];

    if (!I::Cvars) {
        Log("L4NEnv: ICvar unavailable (VEngineCvar007 missing) - L4N coordination disabled");
        return;
    }

    // Presence probe: any l4n_* cvar, or the neko shader DLL, means we are
    // inside an L4N runtime. (Being loaded at all already implies it, but
    // the probe also validates that the cvar system is reachable.)
    const bool byCvar  = Cvar("l4n_game_hud_visible", &m_cvHudVisible) != nullptr
                      || Cvar("l4n_patch_hud_scope", &m_cvPatchHudScope) != nullptr;
    const bool byShader = ::GetModuleHandleA("game_shader_generic_neko.dll") != nullptr;
    m_detected.store(byCvar || byShader, std::memory_order_release);

    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "L4NEnv: runtime %s (cvar probe=%d, neko shader=%d, modules tracked=%zu)",
        m_detected.load(std::memory_order_acquire) ? "DETECTED" : "NOT detected",
        byCvar ? 1 : 0, byShader ? 1 : 0, ModuleCount());
    Log(buf);

    if (!m_detected.load(std::memory_order_acquire)) return;

    // Resolve the rest of the coordinated cvars and report their state
    // once, so "weird scope/crosshair/sway behaviour" reports come with
    // the L4N side of the story attached.
    const bool hudVisible = HudVisible();
    const bool patchScope = PatchHudScope();
    const bool sway = SwayEnabled();
    const bool swayHH = SwayIgnoreHelpingHand();

    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
        "L4NEnv: l4n_game_hud_visible=%d l4n_patch_hud_scope=%d "
        "l4n_vm_sway=%d l4n_vm_sway_ignore_helpinghand=%d",
        hudVisible ? 1 : 0, patchScope ? 1 : 0, sway ? 1 : 0, swayHH ? 1 : 0);
    Log(buf);

    if (patchScope)
        Log("L4NEnv WARN: l4n_patch_hud_scope=1 - L4N renders the scope HUD itself; "
            "if the crosshair misbehaves while scoped, toggle this cvar first");
    if (swayHH)
        Log("L4NEnv WARN: l4n_vm_sway_ignore_helpinghand=1 - L4N's own readme notes "
            "this can strip sway from plugin ADS animations; set it to 0 if ADS "
            "weapon sway looks wrong");
    if (!sway)
        Log("L4NEnv note: l4n_vm_sway=0 - viewmodel sway is off platform-wide, "
            "ADS animations will feel stiffer than usual");
}

} // namespace L4N
