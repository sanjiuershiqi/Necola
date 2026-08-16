#pragma once

#include "Hooks.h"
#include "Vars.h"

#include "Feature/InputManager/InputManager.h"


class CGlobal_ModuleEntry
{
public:
	bool Load();
	void undo();
};

namespace G { inline CGlobal_ModuleEntry ModuleEntry; }
