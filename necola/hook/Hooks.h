#pragma once


#include "Raw/BaseClient/BaseClient.h"
#include "Raw/BaseAnimating/BaseAnimating.h"
#include "Raw/BaseCombatWeapon/BaseCombatWeapon.h"
#include "Raw/EngineVGui/EngineVGui.h"
#include "Raw/GameEventManager/GameEventManager.h"


class CGlobal_Hooks
{
public:
	void Init();
	void undo();
};

namespace G { inline CGlobal_Hooks Hooks; }
