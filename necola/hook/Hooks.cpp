#include "Hooks.h"

#include <string>

using namespace Hooks;

void CGlobal_Hooks::Init()
{
	const MH_STATUS MH_INIT_STATUS = MH_Initialize();


	if (MH_INIT_STATUS == MH_STATUS::MH_OK)
	{
		BaseClient::Init();
		EngineVGui::Init();
		BaseCombatWeapon::Init();
		BaseAnimating::Init();
		GameEventManager::Init();
	}
	MH_EnableHook(MH_ALL_HOOKS);

}

void CGlobal_Hooks::undo()
{

	MH_DisableHook(MH_ALL_HOOKS);
	MH_Uninitialize();
}
