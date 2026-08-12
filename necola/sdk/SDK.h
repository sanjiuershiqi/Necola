#pragma once
#include "../libs/json.hpp"

#include "utils/GameUtil.h"

namespace I {
	inline void* ClientMode = nullptr;
}

struct WeaponSpawnInfo_t
{
	const wchar_t* m_szName;
};
