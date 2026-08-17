#pragma once

#include "../../../sdk/SDK.h"
#include "../../../sdk/l4d2/interfaces/MaterialSystem.h"

#include <array>
#include <cstdint>
#include <string>

class CampaignTimer {
public:
	void OnLevelInitPreEntity(const char* mapName);
	void OnLevelInitPostEntity();
	void OnLevelShutdown();
	void OnMapTransition();
	void OnMissionLost();
	void OnRoundStart();

	void UpdateHudMaterials();
	void Reset();
	void Shutdown();

	std::uint64_t GetCampaignSeconds() const;
	std::uint64_t GetChapterSeconds() const;
	bool IsRunning() const { return m_inLevel; }
	const std::string& GetCurrentMap() const { return m_currentMap; }

private:
	struct HudDigitMaterial {
		const char* name;
		IMaterial* material = nullptr;
		IMaterialVar* frame = nullptr;
	};

	double CurrentChapterSeconds() const;
	void FreezeChapter();
	void StartChapter();
	void InvalidateHudMaterials();
	bool ResolveHudMaterials();

	double m_completedSeconds = 0.0;
	double m_frozenChapterSeconds = 0.0;
	float m_chapterStartTime = 0.0f;
	bool m_started = false;
	bool m_inLevel = false;
	bool m_continueOnNextLevel = false;
	bool m_waitingForRestartRound = false;
	bool m_preserveCompletedOnNextLevel = false;
	std::string m_currentMap;

	std::array<HudDigitMaterial, 6> m_hudDigits{{
		{"vgui/yarou/hud_time/time_hour2"},
		{"vgui/yarou/hud_time/time_hour1"},
		{"vgui/yarou/hud_time/time_min2"},
		{"vgui/yarou/hud_time/time_min1"},
		{"vgui/yarou/hud_time/time_sec2"},
		{"vgui/yarou/hud_time/time_sec1"},
	}};
	bool m_materialsResolved = false;
	bool m_materialResolveAttempted = false;
};

namespace F { inline CampaignTimer CampaignTimerMgr; }
