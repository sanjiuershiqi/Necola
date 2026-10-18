#pragma once
#include <cstdint>

// L4N (Left 4 Neko) plugin SDK — official interface contract.
//
// PROVENANCE: vendored from L4N_extracted/bin/neko/plugins/l4n_plugin.h
// (the L4N release swapped in on 2026-10). Keep the class bodies below a
// literal copy of that header so future SDK bumps are a trivial diff — adapt
// the PLUGIN side instead of editing them here.
//
// CHANGES VS THE PREVIOUSLY VENDORED COPY (v2.43.0 era):
//   * The contract is now two classes: IL4NPluginV1 (the old callback set) and
//     IL4NPlugin : public IL4NPluginV1, which adds RequestHudMenu(bool).
//   * GetInterfaceVersion() returns 2 (the old header returned 1).
//   * NEW: RequestHudMenu lets a plugin contribute an entry to L4N's OWN HUD
//     menu. Returning nullptr means "this plugin has no HUD menu entry", which
//     is what Necola does today: it draws its own VGUI menu
//     (hook/Feature/MenuManager).
//
// Menu contract (from the header comment, translated):
//   RequestHudMenu(true)  -> return the title string to advertise a HUD menu
//                            entry; return nullptr for no entry.
//   RequestHudMenu(false) -> return a KeyValues string describing the menu,
//                            modelled on `custom_commands` in
//                            neko/config_template.vdf, plus a `callback` key
//                            whose value is a function pointer of type
//                                char* (*)(void* user_data)
//                            returning nullptr or a sub-menu KeyValues string.
//   Reserved key "_refresh" "1" refreshes the current menu instead of pushing
//   a new level.
//
// Loader facts confirmed by the header's own sample:
//   * DLL lives in <left4dead2>/bin/neko/plugins/*.dll
//   * it exports the undecorated C symbol GetL4NPluginInstance
//   * OnModuleLoaded's `handle` is the LoadLibrary HMODULE (std::bit_cast)
//   * module_name arrives WITHOUT the ".dll" suffix (e.g. "client", "engine")

class IL4NPluginV1 {
public:

	virtual ~IL4NPluginV1() = default;

	virtual unsigned int GetInterfaceVersion() { return 2; }

	virtual const char* GetName() { return "MyPlugin"; }
	virtual const char* GetVersion() { return "1.0"; }

	virtual void OnModuleLoaded(const char* module_name, std::uintptr_t handle) {};
	virtual void OnGameLaunch() {};

	virtual void OnD3DCreated(void* d3d) {};
	virtual void OnD3DDeviceCreated(void* d3d_device, bool is_dxvk) {};
};


class IL4NPlugin : public IL4NPluginV1 {

public:	// version 2
	/*
		title为真时返回标题字符串，明确该插件需要HUD菜单入口，否则返回nullptr
		提供标题之后，当用户请求进入菜单，会再次调用，用于返回菜单结构
		title为假时，返回定义菜单结构的KeyValues字符串，格式基于neko/config_template.vdf里的custom_commands
		额外添加了callback键来提供回调函数功能，也可以用来实现子菜单
		回调函数的定义如下：
			chost char* my_callback(void* user_data) {
				返回nullptr或者子菜单的KeyValues字符串
			}
		示例KeyValues字符串：
		"我的标题" {
			// 保留关键字，用于描述特殊信息(可选)
			"_refresh" "1"	// 刷新当前菜单，而不是作为新菜单堆到下一级

			// 菜单项目描述
			"项目1" { "cvar" "my_cvar_name" }
			"使用回调函数的项目" {
				"callback" "0x12345"
				"user_data"	"0x12345"	// (可选)
			}
		}
	*/
	virtual const char* RequestHudMenu(bool request_title) {
		return nullptr;
	}
};

typedef IL4NPlugin* (*GetL4NPluginInstanceFunc)();
