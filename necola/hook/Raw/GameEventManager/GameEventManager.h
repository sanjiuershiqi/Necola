#pragma once

#include "../../../sdk/SDK.h"
#include "../../Vars.h"
#include <spdlog/spdlog.h>

namespace Hooks
{
	namespace GameEventManager
	{
		inline Hook::CTable Table;

		namespace FireEventClient
		{
			using FN = bool(__fastcall*)(void*, void*, IGameEvent*);
			constexpr uint32_t Index = 8u;

			bool __fastcall Detour(void* ecx, void* edx,  IGameEvent *event);
		}
		bool Init();
	}
}
