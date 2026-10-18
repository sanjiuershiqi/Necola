#pragma once

// skeeto's "particle platform", ported to the L4N plugin.
//
// Why it exists: overlay materials and sounds resolve through the engine's
// virtual file system, so any addon works for them. A .pcf is different — the
// engine only knows a particle system once it has parsed a particles/*.txt
// manifest that names the .pcf. That is the reason kill-effect particles can
// "require a specific pack".
//
// skeeto solves it by inline-detouring the engine's manifest parser
// (SKT_Hook_ParticleManifestParser / SKT_Particle_LoadManifest): the detour
// calls the original parser first, then enumerates particles/skeeto_*.txt over
// all search paths and calls the engine's AddManifest(manager, "particles/<f>")
// for each match. This module is the same flow, with two deliberate changes:
//
//  * MinHook provides the trampoline instead of skeeto's hand-rolled 6-byte
//    patch + 32-byte VirtualAlloc trampoline, so there is no prologue sniffing
//    and no architecture-specific byte juggling.
//  * Both engine functions are taken from offsets the plugin ALREADY resolves
//    by signature scan (U::Offsets.m_dwGetParticleManifest /
//    m_dwGetParticleManifest2). Those two patterns are byte-compatible with
//    skeeto's own off_10287A54 / off_10287A5C — the SDK pins the operands where
//    skeeto wildcards them.
//
// The engine-side pieces are still pattern-derived, so every call is SEH
// guarded and a fault disables the feature permanently instead of crashing.
namespace ParticlePlatform {

// Idempotent. Call once, after G::Hooks.Init() has initialised MinHook.
bool Install();

// Removes the hook (safe to call when never installed).
void Shutdown();

bool IsInstalled();

} // namespace ParticlePlatform
