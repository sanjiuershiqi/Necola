#pragma once

#include "../../../sdk/SDK.h"

namespace Hooks
{
	namespace BaseCombatWeapon
	{
		namespace SendWeaponAnim
		{
			inline Hook::CFunction Func;
			using FN = bool(__fastcall*)(C_BaseCombatWeapon*, void*, int);
			bool __fastcall Detour(C_BaseCombatWeapon* pThis, void* edx, int a2);
		}

		namespace SetIdealActivity
		{
			inline Hook::CFunction Func;
			using FN = bool(__fastcall*)(C_BaseCombatWeapon*, void*, int);
			bool __fastcall Detour(C_BaseCombatWeapon* pThis, void* edx, int a2);
		}


		void Init();
	}
}
