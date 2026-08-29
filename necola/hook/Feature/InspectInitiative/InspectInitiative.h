#pragma once

#include "../../../sdk/SDK.h"
#include <unordered_map>

class InspectInitiative {
public:
	void LoadConfig(const nlohmann::json& doc);
	void SaveConfig(nlohmann::json& doc) const;
	void Bind();
	void Shutdown();
	void Reset();
	void Trigger();
	void FrameUpdate();
	bool IsBound() const { return m_bound; }

private:
	bool m_bound = false;
	std::uint32_t m_boundKey = 0;
	std::unordered_map<int, int> m_maxAmmo;
};

namespace F { inline InspectInitiative InspectMgr; }
