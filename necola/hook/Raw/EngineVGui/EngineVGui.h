#pragma once

#include "../../../sdk/SDK.h"

namespace Hooks
{
	namespace EngineVGui
	{
		inline Hook::CTable Table;
		namespace Paint
		{
			using FN = void(__fastcall*)(void*, void*, int);
			constexpr uint32_t Index = 14u;

			void __fastcall Detour(void* ecx, void* edx, int mode);
		}

		void Init();

		// Restore the user's crosshair value if we still have it forced
		// off at unload time (called from ModuleEntry::undo).
		void RestoreCrosshairForUnload();
	}
}
