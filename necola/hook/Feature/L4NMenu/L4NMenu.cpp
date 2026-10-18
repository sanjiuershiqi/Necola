#include "L4NMenu.h"

#include "../../Vars.h"
#include "../../../diag.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// KFLog lives inside KillFeedback.cpp's anonymous namespace, so this module
// logs through the always-available diagnostic sink instead.
void MenuLog(const char* format, ...) {
	char line[256] = {};
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	NecolaDiagLog(line);
}

// ---- item table -----------------------------------------------------------
// Only non-ADS settings live here for now: ADS keeps using its own VGUI pages
// (the user asked for everything EXCEPT ADS to move over).
struct ToggleDesc {
	const char* label;
	bool* flag;
};

ToggleDesc g_toggles[] = {
	{ "启用击杀提示", &G::Vars.killFeedbackEnabled },
	{ "击杀图标/粒子", &G::Vars.killFeedbackIcon },
	{ "击杀音效", &G::Vars.killFeedbackSound },
	{ "普通感染者反馈", &G::Vars.killFeedbackCommon },
	{ "特殊感染者反馈", &G::Vars.killFeedbackSpecial },
	{ "伤害数字", &G::Vars.hitFeedbackEnabled },
	{ "准星命中标记", &G::Vars.hitFeedbackHitMarker },
	{ "命中数字（伤害数值）", &G::Vars.hitFeedbackNumbers },
	{ "普感伤害数字", &G::Vars.hitFeedbackCommon },
	{ "枪械击杀", &G::Vars.killFeedbackFirearm },
	{ "爆头击杀", &G::Vars.killFeedbackHeadshot },
	{ "近战击杀", &G::Vars.killFeedbackMelee },
	{ "爆炸击杀", &G::Vars.killFeedbackExplosion },
	{ "连杀特效", &G::Vars.killFeedbackMultiKill },
	{ "特感视觉优先", &G::Vars.killFeedbackSiDedicated },
	{ "特感音效优先", &G::Vars.killFeedbackSiSound },
	{ "Tank 反馈", &G::Vars.killFeedbackTank },
	{ "Witch 反馈", &G::Vars.killFeedbackWitch },
};
constexpr int kToggleCount = static_cast<int>(sizeof(g_toggles) / sizeof(g_toggles[0]));

// Action ids. Toggles use 1..kToggleCount; anything else is special-cased.
constexpr int kActionHitMode = 100;
constexpr int kActionSoundVolume = 101;

// ---- keyvalues helpers ----------------------------------------------------
std::string KvQuote(const std::string& text) {
	std::string out;
	out.reserve(text.size() + 8);
	for (char c : text) {
		if (c == '"' || c == '\\') out.push_back('\\');
		out.push_back(c);
	}
	return out;
}

std::string KvUIntPtr(const void* pointer) {
	char buffer[32] = {};
	_snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%llX",
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(pointer)));
	return buffer;
}

const char* HitModeLabel() {
	switch (G::Vars.killFeedbackHitMode) {
		case 0: return "主题命中：关";
		case 1: return "主题命中：仅特感";
		default: return "主题命中：全部";
	}
}

std::string BuildRootKv(bool refresh);

// One callback for every callback-driven item. L4N passes back the pointer
// encoded in "user_data", so the id round-trips without any registration.
char* NecolaMenuCallback(void* user_data) {
	const int action = static_cast<int>(reinterpret_cast<std::intptr_t>(user_data));
	if (action >= 1 && action <= kToggleCount) {
		bool& flag = *g_toggles[action - 1].flag;
		flag = !flag;
		MenuLog("l4n menu: toggled '%s' -> %d", g_toggles[action - 1].label, flag ? 1 : 0);
	} else if (action == kActionHitMode) {
		G::Vars.killFeedbackHitMode = (G::Vars.killFeedbackHitMode + 1) % 3;
		MenuLog("l4n menu: hit mode -> %d", G::Vars.killFeedbackHitMode);
	} else if (action == kActionSoundVolume) {
		static const int steps[5] = {0, 25, 50, 75, 100};
		int next = 100;
		for (int i = 0; i < 5; ++i) {
			if (steps[i] == G::Vars.killFeedbackSoundVolume) {
				next = steps[(i + 1) % 5];
				break;
			}
		}
		G::Vars.killFeedbackSoundVolume = next;
		MenuLog("l4n menu: sound volume -> %d", next);
	}
	// Returning the same tree with "_refresh" asks L4N to refresh in place
	// instead of stacking a new submenu (see the contract in l4n_plugin.h).
	static std::string cache;
	cache = BuildRootKv(/*refresh=*/true);
	return const_cast<char*>(cache.c_str());
}

std::string BuildRootKv(bool refresh) {
	const std::string callback = KvUIntPtr(reinterpret_cast<const void*>(&NecolaMenuCallback));

	std::string kv;
	kv += "\"Necola\"\n{\n";
	if (refresh) kv += "\t\"_refresh\" \"1\"\n";

	for (int i = 0; i < kToggleCount; ++i) {
		const ToggleDesc& item = g_toggles[i];
		kv += "\t\"";
		kv += KvQuote(std::string(item.label) + (*item.flag ? " [开]" : " [关]"));
		kv += "\"\n\t{\n\t\t\"callback\" \"";
		kv += callback;
		kv += "\"\n\t\t\"user_data\" \"";
		kv += KvUIntPtr(reinterpret_cast<const void*>(static_cast<std::intptr_t>(i + 1)));
		kv += "\"\n\t}\n";
	}

	kv += "\t\"";
	kv += KvQuote(HitModeLabel());
	kv += "\"\n\t{\n\t\t\"callback\" \"";
	kv += callback;
	kv += "\"\n\t\t\"user_data\" \"";
	kv += KvUIntPtr(reinterpret_cast<const void*>(static_cast<std::intptr_t>(kActionHitMode)));
	kv += "\"\n\t}\n";

	// Sound volume cycles 0/25/50/75/100 like the VGUI menu.
	{
		char label[64] = {};
		_snprintf_s(label, sizeof(label), _TRUNCATE, "音效音量：%d%%", G::Vars.killFeedbackSoundVolume);
		kv += "\t\"";
		kv += KvQuote(label);
		kv += "\"\n\t{\n\t\t\"callback\" \"";
		kv += callback;
		kv += "\"\n\t\t\"user_data\" \"";
		kv += KvUIntPtr(reinterpret_cast<const void*>(static_cast<std::intptr_t>(kActionSoundVolume)));
		kv += "\"\n\t}\n";
	}

	// Keep a door back to the full VGUI menu (ADS pages live there).
	kv += "\t\"打开 Necola 完整菜单\"\n\t{\n\t\t\"cmd\" \"necola_menu\"\n\t}\n";
	kv += "}\n";
	return kv;
}

std::string g_cache;

} // namespace

namespace L4NMenu {

const char* Title() {
	// Returning nullptr would tell L4N "this plugin has no HUD menu entry".
	return "Necola";
}

const char* Root() {
	g_cache = BuildRootKv(/*refresh=*/false);
	return g_cache.c_str();
}

void Invalidate() {
	g_cache.clear();
}

} // namespace L4NMenu
