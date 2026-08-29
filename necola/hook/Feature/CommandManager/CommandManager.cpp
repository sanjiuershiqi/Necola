#include "CommandManager.h"
#include "../../Vars.h"

#include <cstdlib>


void CommandManager::RegistCommand(const char* cmd, FnCommandCallback_t fnCallback, const char* pHelpString) {
	if (!cmd || !*cmd || !fnCallback || !U::Offsets.m_dwStaticConCommand) return;
	for (const ConCommand* existing : m_commands) {
		if (existing && existing->m_pszName && _stricmp(existing->m_pszName, cmd) == 0) return;
	}
	ConCommand* pCmd = (ConCommand*)malloc(sizeof(ConCommand));
	if (!pCmd) return;
	memset(pCmd, 0, sizeof(ConCommand));
	StaticCommand(pCmd, cmd, fnCallback, pHelpString, FCVAR_CLIENTCMD_CAN_EXECUTE);
	m_commands.push_back(pCmd);
}

void CommandManager::Shutdown() {
	if (I::Cvars) {
		for (ConCommand* command : m_commands) {
			if (command) I::Cvars->UnregisterConCommand(command);
		}
	}
	for (ConCommand* command : m_commands) std::free(command);
	m_commands.clear();
}


void CommandManager::StaticCommand(void* pThis, const char* cmd, FnCommandCallback_t fnCallback, const char* pHelpString, int flag)
{
	return reinterpret_cast<void(__thiscall*)(void*, const char*, void*, const char*, int, int)>(U::Offsets.m_dwStaticConCommand)(pThis, cmd, fnCallback, pHelpString, flag, 0);
}

