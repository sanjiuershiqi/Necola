#pragma once
#include "../../../sdk/SDK.h"
#include <functional>

enum class m_state
{
	none = 0,
	down,
	up,
	pressed
};


class CGlobal_InputManager {
public:
	bool Init();
	void undo();

	m_state GetKeyState(uint32_t vk);

	bool IsKeyDown(uint32_t vk);
	bool WasKeyPressed(uint32_t vk);

	void AddHotkey(uint32_t vk, std::function<void(void)> f);
	void RemHotkey(uint32_t vk);

	HWND GetWindow();
	WNDPROC GetWndProcOriginal();

	bool ProcessMessage(UINT, WPARAM, LPARAM);
	bool ProcessMouseMessage(UINT, WPARAM, LPARAM);
	bool ProcessKeybdMessage(UINT, WPARAM, LPARAM);
private:
	static constexpr std::uint32_t kKeyCapacity = 256;
	HWND                                     m_hwnd = nullptr;
	WNDPROC                                  m_old_wnd_proc = nullptr;
	bool                                     m_windowHooked = false;
	m_state                                  m_key_map[kKeyCapacity]{};
	std::function<void(void)>                m_hotkeys[kKeyCapacity]{};
};



namespace G { inline CGlobal_InputManager InputManagerI; }
