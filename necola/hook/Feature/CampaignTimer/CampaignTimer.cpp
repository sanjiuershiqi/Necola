#include "CampaignTimer.h"

#include <algorithm>
#include <cmath>

namespace {
float EngineTime() {
	return I::GlobalVars ? I::GlobalVars->curtime : 0.0f;
}
}

double CampaignTimer::CurrentChapterSeconds() const {
	if (!m_inLevel || !I::GlobalVars) return m_frozenChapterSeconds;
	const double activeSeconds = std::max(0.0, static_cast<double>(EngineTime() - m_chapterStartTime));
	return m_frozenChapterSeconds + activeSeconds;
}

std::uint64_t CampaignTimer::GetCampaignSeconds() const {
	return static_cast<std::uint64_t>(std::floor(std::max(0.0, m_completedSeconds + CurrentChapterSeconds())));
}

std::uint64_t CampaignTimer::GetChapterSeconds() const {
	return static_cast<std::uint64_t>(std::floor(CurrentChapterSeconds()));
}

void CampaignTimer::StartChapter() {
	m_frozenChapterSeconds = 0.0;
	m_chapterStartTime = EngineTime();
	m_started = true;
	m_inLevel = I::GlobalVars != nullptr;
}

void CampaignTimer::FreezeChapter() {
	if (!m_inLevel) return;
	m_frozenChapterSeconds = CurrentChapterSeconds();
	m_inLevel = false;
}

void CampaignTimer::OnLevelInitPreEntity(const char* mapName) {
	const std::string nextMap = mapName ? mapName : "";
	const bool restartingSameMap =
		m_preserveCompletedOnNextLevel && !m_currentMap.empty() && m_currentMap == nextMap;

	if (m_started && !m_continueOnNextLevel && !restartingSameMap) {
		m_completedSeconds = 0.0;
	}

	m_currentMap = nextMap;
	m_inLevel = false;
	m_frozenChapterSeconds = 0.0;
	m_continueOnNextLevel = false;
	m_waitingForRestartRound = false;
	m_preserveCompletedOnNextLevel = false;
	InvalidateHudMaterials();
}

void CampaignTimer::OnLevelInitPostEntity() {
	StartChapter();
}

void CampaignTimer::OnLevelShutdown() {
	FreezeChapter();
	InvalidateHudMaterials();
}

void CampaignTimer::OnMapTransition() {
	FreezeChapter();
	m_completedSeconds += m_frozenChapterSeconds;
	m_frozenChapterSeconds = 0.0;
	m_continueOnNextLevel = true;
	m_waitingForRestartRound = false;
	m_preserveCompletedOnNextLevel = false;
}

void CampaignTimer::OnMissionLost() {
	if (!m_started) return;
	m_inLevel = false;
	m_frozenChapterSeconds = 0.0;
	m_continueOnNextLevel = false;
	m_waitingForRestartRound = true;
	m_preserveCompletedOnNextLevel = true;
}

void CampaignTimer::OnRoundStart() {
	if (!m_waitingForRestartRound) return;
	StartChapter();
	m_waitingForRestartRound = false;
}

void CampaignTimer::Reset() {
	m_completedSeconds = 0.0;
	m_frozenChapterSeconds = 0.0;
	m_continueOnNextLevel = false;
	m_waitingForRestartRound = false;
	m_preserveCompletedOnNextLevel = false;
	if (I::EngineClient && I::EngineClient->IsConnected() && I::EngineClient->IsInGame()) {
		StartChapter();
	} else {
		m_started = false;
		m_inLevel = false;
		m_chapterStartTime = 0.0f;
		m_currentMap.clear();
	}
}

void CampaignTimer::Shutdown() {
	m_inLevel = false;
	InvalidateHudMaterials();
}

void CampaignTimer::InvalidateHudMaterials() {
	for (auto& digit : m_hudDigits) {
		digit.frame = nullptr;
		if (digit.material) digit.material->DecrementReferenceCount();
		digit.material = nullptr;
	}
	m_materialsResolved = false;
	m_materialResolveAttempted = false;
}

bool CampaignTimer::ResolveHudMaterials() {
	if (m_materialsResolved) return true;
	if (m_materialResolveAttempted || !I::MaterialSystem) return false;
	m_materialResolveAttempted = true;

	for (auto& digit : m_hudDigits) {
		IMaterial* material = I::MaterialSystem->FindMaterial(digit.name, TEXTURE_GROUP_VGUI, false);
		if (!material || material->IsErrorMaterial()) {
			for (auto& resolvedDigit : m_hudDigits) {
				resolvedDigit.frame = nullptr;
				if (resolvedDigit.material) resolvedDigit.material->DecrementReferenceCount();
				resolvedDigit.material = nullptr;
			}
			return false;
		}

		material->IncrementReferenceCount();
		digit.material = material;
		bool found = false;
		digit.frame = material->FindVar("$frame", &found, false);
		if (!found || !digit.frame) {
			for (auto& resolvedDigit : m_hudDigits) {
				resolvedDigit.frame = nullptr;
				if (resolvedDigit.material) resolvedDigit.material->DecrementReferenceCount();
				resolvedDigit.material = nullptr;
			}
			return false;
		}
	}

	m_materialsResolved = true;
	return true;
}

void CampaignTimer::UpdateHudMaterials() {
	if (!m_started || !m_inLevel || !ResolveHudMaterials()) return;

	const std::uint64_t total = GetCampaignSeconds();
	const int hours = static_cast<int>((total / 3600) % 100);
	const int minutes = static_cast<int>((total / 60) % 60);
	const int seconds = static_cast<int>(total % 60);
	const int frames[] = {
		hours / 10,
		hours % 10,
		minutes / 10,
		minutes % 10,
		seconds / 10,
		seconds % 10,
	};

	for (std::size_t i = 0; i < m_hudDigits.size(); ++i) {
		m_hudDigits[i].frame->SetIntValue(frames[i]);
	}
}
