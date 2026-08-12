#pragma once
#include <cstdint>

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

/*
	// bin/neko/plugins/MyPlugin.dll

	class MyPlugin : public IL4NPlugin {
	public:
		void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
			std::string_view name = module_name;

			auto hModule = std::bit_cast<HMODULE>(handle);
			if (name == "client") {
				// do some thing...
			} else if (name == "engine") {
				// do some thing...
			}
		}
	};

	extern "C" __declspec(dllexport) IL4NPlugin* GetL4NPluginInstance() {
		static MyPlugin instance;
		return &instance;
	}

*/