#pragma once
#include "../../../sdk/SDK.h"
#include <string>
#include <vector>


class CommandManager {
public:
	void RegistCommand(const char* cmd, FnCommandCallback_t fnCallback, const char* pHelpString);
	void Shutdown();


private:
	void StaticCommand(void* pThis, const char* cmd, FnCommandCallback_t fnCallback, const char* pHelpString, int flag);
	std::vector<ConCommand*> m_commands;
};



namespace F { inline CommandManager CmdMgr; }
