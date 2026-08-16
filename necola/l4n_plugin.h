#pragma once
#include <cstdint>

// L4N (Left 4 Neko) plugin SDK — official interface contract.
// Source: bin/neko/plugins/l4n_plugin.h (shipped with L4N v2.43.0)
//
// A plugin is a DLL placed in <left4dead2>/neko/plugins/*.dll that exports
// GetL4NPluginInstance. L4N (a patched left4dead2.exe, NOT an injected DLL)
// scans that directory on startup, LoadLibraryExA's each DLL,
// GetProcAddress("GetL4NPluginInstance"), and calls it to obtain an
// IL4NPlugin instance. L4N then drives the plugin via the virtual callbacks
// below (OnModuleLoaded / OnGameLaunch / OnD3D*).
//
// Official example (from the v2.43.0 header comment): OnModuleLoaded's
// `handle` is the LoadLibrary HMODULE — std::bit_cast<HMODULE>(handle) —
// and module_name compares WITHOUT the ".dll" suffix (e.g. "client").
//
// Interface version: 1 (unchanged since the plugin system appeared ~2.34.0).

class IL4NPlugin {

public:

	virtual ~IL4NPlugin() = default;

	virtual unsigned int GetInterfaceVersion() { return 1; }
	virtual const char* GetName() { return "MyPlugin"; }
	virtual const char* GetVersion() { return "1.0"; }

	virtual void OnModuleLoaded(const char* module_name, std::uintptr_t handle) {};
	virtual void OnGameLaunch() {};

	virtual void OnD3DCreated(void* d3d) {};
	virtual void OnD3DDeviceCreated(void* d3d_device, bool is_dxvk) {};
};

typedef IL4NPlugin* (*GetL4NPluginInstanceFunc)();
