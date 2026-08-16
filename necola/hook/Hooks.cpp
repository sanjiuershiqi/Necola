#include "Hooks.h"

#include <string>

using namespace Hooks;

bool CGlobal_Hooks::Init()
{
	const MH_STATUS MH_INIT_STATUS = MH_Initialize();
	if (MH_INIT_STATUS != MH_STATUS::MH_OK) return false;
	m_initialized = true;

	bool ok = BaseClient::Init();
	ok = EngineVGui::Init() && ok;
	ok = BaseCombatWeapon::Init() && ok;
	ok = BaseAnimating::Init() && ok;
	ok = GameEventManager::Init() && ok;
	if (!ok || MH_EnableHook(MH_ALL_HOOKS) != MH_STATUS::MH_OK) {
		MH_DisableHook(MH_ALL_HOOKS);
		if (MH_Uninitialize() == MH_STATUS::MH_OK) m_initialized = false;
		return false;
	}

	return true;
}

void CGlobal_Hooks::undo()
{
	if (!m_initialized) return;
	MH_DisableHook(MH_ALL_HOOKS);
	if (MH_Uninitialize() == MH_STATUS::MH_OK) m_initialized = false;
}
