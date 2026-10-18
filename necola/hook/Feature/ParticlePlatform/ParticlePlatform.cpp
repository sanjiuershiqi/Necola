#include "ParticlePlatform.h"

#include "../../../sdk/SDK.h"
#include "../../../sdk/utils/Offsets.h"
#include "../../../sdk/l4d2/interfaces/FileSystem.h"
#include "../../Vars.h"
#include "../../../diag.h"
#include "../../../sdk/utils/Hook.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

namespace {

// skeeto caps a single append pass at 16 manifests (SKT_FileSystem_EnumMatches
// is called with max = 0x10) and matches the same wildcard.
constexpr int kMaxManifestFiles = 16;
constexpr const char* kManifestPattern = "particles/skeeto_*.txt";

Hook::CFunction g_parserHook;
bool g_installed = false;
bool g_appendDisabled = false;
bool g_loggedNothing = false;

void PPLog(const char* format, ...) {
	char line[256] = {};
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	NecolaDiagLog(line);
}

// Mirrors skeeto's SKT_Particle_LoadManifest tail: enumerate every matching
// manifest across the search paths (loose first, then each mounted VPK) and ask
// the engine to add it.
//
// Kept free of anything requiring unwinding because MSVC rejects __try in such
// a frame (C2712); __declspec(noinline) keeps that property even if the caller
// gets inlined. The engine pointer and its AddManifest sibling are both
// pattern-derived, so a fault must be survivable: it disables the feature
// permanently rather than retrying every manifest parse.
__declspec(noinline) int AppendManifests(void* manager) {
	if (!manager || !I::FileSystem || !U::Offsets.m_dwGetParticleManifest2) return 0;

	int added = 0;
	FileFindHandle_t handle = -1;
	__try {
		const char* name = I::FileSystem->FindFirst(kManifestPattern, &handle);
		while (name && name[0] && added < kMaxManifestFiles) {
			// Same filters as skeeto: skip directories, empty names and "." / "..".
			if (name[0] != '.' && !I::FileSystem->FindIsDirectory(handle)) {
				// FindFirst yields a bare file name, so the directory has to be
				// re-prefixed — exactly what SKT_Particle_FormatPath does.
				char full[260] = {};
				_snprintf_s(full, sizeof(full), _TRUNCATE, "particles/%s", name);
				using AddManifestFn = int(__cdecl*)(void*, const char*);
				const AddManifestFn add =
					reinterpret_cast<AddManifestFn>(U::Offsets.m_dwGetParticleManifest2);
				add(manager, full);
				++added;
			}
			name = I::FileSystem->FindNext(handle);
		}
		if (handle != -1) I::FileSystem->FindClose(handle);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		g_appendDisabled = true;
		return -1;
	}
	return added;
}

int ObserveAppend(void* manager) {
	if (g_appendDisabled) return 0;
	const int added = AppendManifests(manager);
	if (added < 0) {
		PPLog("particle platform: manifest append faulted; feature disabled");
		return 0;
	}
	if (added > 0) {
		PPLog("particle platform: appended %d manifest(s) matching %s", added, kManifestPattern);
	} else if (!g_loggedNothing) {
		PPLog("particle platform: no manifest matching %s", kManifestPattern);
		g_loggedNothing = true;
	}
	return added;
}

// The engine's manifest parser takes exactly one stack argument (the particle
// manager) — confirmed from skeeto's detour, which forwards a single pushed
// value to the trampoline, and from the target's own prologue
// (55 8B EC 8B 45 08 ...).
int __cdecl ParserDetour(void* manager) {
	using ParserFn = int(__cdecl*)(void*);
	const ParserFn original = g_parserHook.Original<ParserFn>();
	const int result = original ? original(manager) : 0;
	ObserveAppend(manager);
	return result;
}

} // namespace

namespace ParticlePlatform {

bool Install() {
	if (g_installed) return true;
	if (!U::Offsets.m_dwGetParticleManifest || !U::Offsets.m_dwGetParticleManifest2) {
		PPLog("particle platform: manifest offsets missing (manifest=%p add=%p); skip",
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(U::Offsets.m_dwGetParticleManifest)),
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(U::Offsets.m_dwGetParticleManifest2)));
		return false;
	}
	void* target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(U::Offsets.m_dwGetParticleManifest));
	if (!g_parserHook.Init(target, reinterpret_cast<void*>(&ParserDetour))) {
		PPLog("particle platform: MH_CreateHook failed for manifest parser %p", target);
		return false;
	}
	if (MH_EnableHook(target) != MH_OK) {
		PPLog("particle platform: MH_EnableHook failed for manifest parser %p", target);
		return false;
	}
	g_installed = true;
	PPLog("particle platform: hook installed parser=%p add=%p (pattern %s)", target,
		reinterpret_cast<void*>(static_cast<std::uintptr_t>(U::Offsets.m_dwGetParticleManifest2)),
		kManifestPattern);
	return true;
}

void Shutdown() {
	if (!g_installed) return;
	void* target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(U::Offsets.m_dwGetParticleManifest));
	MH_DisableHook(target);
	g_installed = false;
	PPLog("particle platform: hook removed");
}

bool IsInstalled() {
	return g_installed;
}

} // namespace ParticlePlatform
