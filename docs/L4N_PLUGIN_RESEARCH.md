# L4N 插件机制逆向研究

> 本文档记录对 L4N (Left 4 Neko) 平台的逆向分析过程与结论。这是本项目存在的根基——不理解 L4N 如何加载插件，就无法正确实现初始化。

## 一、L4N 是什么

L4N（Left 4 Neko）是 Left 4 Dead 2 的修改平台，类比 CS:GO 的 cheat loader。它本身是一个被注入到游戏进程的 DLL/ASI，在游戏进程内提供：
- 模块加载追踪（Source 引擎分阶段加载 DLL）
- 插件加载机制（扫描 `neko/plugins/*.dll`）
- 自身的功能（渲染钩子、配置系统等）

**关键文件分布**（L4N 安装后）：
```
<left4dead2>/
├── left4dead2.exe                # 游戏主程序
├── neko/
│   ├── config.vdf                # L4N主配置（VDF格式）
│   ├── config_template.vdf       # 配置模板（含注释）
│   ├── l4ngui_schinese.vdf       # 中文GUI本地化
│   └── plugins/
│       └── necola_ads.dll        # ← 本插件
└── left4dead2/bin/               # 引擎DLL
    ├── client.dll, engine.dll ...
```

## 二、插件接口契约（逆向所得）

### 2.1 接口来源
`IL4NPlugin` 接口定义来自 L4N v2.41.0 附带的 `l4n_plugin.h`（随 L4N 发布在 `bin/neko/plugins/l4n_plugin.h`）。

完整定义见 [necola/l4n_plugin.h](necola/l4n_plugin.h)，核心：

```cpp
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
```

### 2.2 插件加载流程（L4N 侧）
经逆向分析（日志观测 + 行为验证），L4N 加载插件的流程：

1. **启动扫描**：游戏启动后，L4N 扫描 `neko/plugins/*.dll`
2. **加载 DLL**：对每个 DLL 调用 `LoadLibraryExA`，触发插件的 `DllMain(DLL_PROCESS_ATTACH)`
3. **获取工厂函数**：`GetProcAddress(hModule, "GetL4NPluginInstance")`
4. **实例化**：调用 `GetL4NPluginInstance()` 获得 `IL4NPlugin*`
5. **生命周期回调**：L4N 按游戏事件调用虚函数

### 2.3 回调时机（实测）
通过日志时间戳分析（见历史日志），回调顺序：

```
DllMain(DLL_PROCESS_ATTACH)           # LoadLibrary 触发
GetL4NPluginInstance() called        # L4N 获取实例
OnGameLaunch()                       # 游戏启动，引擎模块尚未加载
OnModuleLoaded("engine", ...)        # 引擎模块逐个加载
OnModuleLoaded("inputsystem", ...)
OnModuleLoaded("materialsystem", ...)
... (约 20+ 个模块)
OnModuleLoaded("client", ...)        # client.dll 加载（关键节点）
OnModuleLoaded("server", ...)
OnModuleLoaded("gameui", ...)
OnModuleLoaded("vaudio_miles", ...)  # 较晚
OnModuleLoaded("serverbrowser", ...) # 最晚之一
```

**关键发现**：
- `OnGameLaunch` 触发时，**所有引擎模块尚未加载**（`GetModuleHandleA("client.dll")` 返回 NULL）
- `OnModuleLoaded` 是**模块就绪的事件源**，但 L4N **不回调所有模块**（例如 `filesystem_stdio.dll` 从不上报）

## 三、模块就绪等待机制

### 3.1 问题
Necola 需要在 `client.dll`、`engine.dll` 等关键模块加载完成后才能调用 `CreateInterface` 获取引擎接口。但 `OnGameLaunch` 触发太早，模块还没加载。

### 3.2 方案演进

**方案 A（失败）**：在 `OnGameLaunch` 里直接初始化
- 问题：模块未加载，`CreateInterface` 返回 NULL，后续全部崩溃

**方案 B（初版）**：开线程 + 1秒轮询 `GetModuleHandleA`
- 问题：平均浪费 0.5~1 秒；L4N 已提供事件源却没用上

**方案 C（当前）**：条件变量 + 事件驱动 + 兜底轮询
- L4N 的 `OnModuleLoaded` 回调 `notify_all` 唤醒等待线程
- 同时保留 100ms `GetModuleHandleA` 轮询（兜底，因 L4N 不回调 `filesystem_stdio` 等模块）

### 3.3 当前实现
见 [necola/dllmain.cpp](necola/dllmain.cpp)。核心：

```cpp
// 等待的关键模块
const char* required[] = {
    "client.dll", "engine.dll", "vgui2.dll", "datacache.dll",
    "vguimatsurface.dll", "inputsystem.dll", "filesystem_stdio.dll"
};

// 条件变量：L4N 回调时 notify，等待线程被唤醒立即复查
std::condition_variable g_moduleCv;
std::mutex g_moduleMtx;

void OnModuleLoaded(const char* name, std::uintptr_t handle) override {
    {
        std::lock_guard<std::mutex> lk(g_moduleMtx);
        // 记录已加载模块
    }
    g_moduleCv.notify_all();  // 唤醒等待线程
}

// InitThreadFunc 等待逻辑
{
    std::unique_lock<std::mutex> lk(g_moduleMtx);
    g_moduleCv.wait_for(lk, std::chrono::milliseconds(100), []{
        return AllRequiredModulesPresent();  // 检查 7 个模块
    });
}
```

### 3.4 单线程保证
`OnGameLaunch` 和每个 `OnModuleLoaded` 都可能触发初始化。用 `std::call_once` 保证全局只启动一个初始化线程：

```cpp
static std::once_flag s_initOnce;

void OnGameLaunch() override {
    std::call_once(s_initOnce, []{
        CreateThread(NULL, 0, &InitThreadFunc, nullptr, 0, NULL);
    });
}
void OnModuleLoaded(const char* name, std::uintptr_t handle) override {
    std::call_once(s_initOnce, []{
        CreateThread(NULL, 0, &InitThreadFunc, nullptr, 0, NULL);
    });
}
```

**历史教训**：曾因两个回调各启一个线程，导致 `ModuleEntry.Load()` 并发执行，MinHook 重复初始化崩溃。

## 四、config.vdf 研究

### 4.1 VDF 格式
L4N 使用 Valve 的 VDF（KeyValues）格式配置，主文件 `neko/config.vdf`，模板 `config_template.vdf`。

### 4.2 key_bind_acts（按键绑定扩展点）
经逆向 L4N 官方模板（[config_template.vdf](L4N_extracted/left4dead2/neko/config_template.vdf)），确认存在 `key_bind_acts` 段：

```
// 游戏设置-按键绑定的自定义项目
"key_bind_acts" {
    "自定义命令菜单"   "l4n_custom_command_menu"
    "切换第三人称"     "thirdpersonshoulder"
    ...
}
```

**关键发现**（纠正早期错误推测）：
- 模板内含 `thirdpersonshoulder`（引擎原生命令，非 `l4n_*`）
- 证明 L4N **不校验命令来源**，任意命令字符串都能进列表
- 引擎键盘设置列表按命令字符串工作，条目显示**不要求命令已注册**

**使用方法**：在 `config.vdf` 的 `key_bind_acts` 段添加：
```
"key_bind_acts" {
    "Necola ADS 开镜"      "necola_ads"
    "Necola 菜单"          "necola_menu"
}
```
重启后游戏"选项→键盘/鼠标→按键设置"会出现条目。

### 4.3 custom_commands（命令菜单）
L4N 提供 `l4n_custom_command_menu` 命令打开自定义命令菜单，通过 `custom_commands` 段配置。但经分析，该菜单对 `l4n_*` 命令可靠，第三方命令行为不确定。

### 4.4 Necola 的配置策略
Necola 不依赖 L4N 的 VDF 配置系统，而是用自己的 JSON 配置（`neko/FeatureConfig.json`），原因：
1. JSON 比 VDF 更易读写（nlohmann/json）
2. 自带菜单 UI 可直接修改配置
3. 提供 `KeyBinds` 数组实现自动 `bind`（不依赖 L4N config.vdf）

## 五、L4N 修改对引擎的影响

### 5.1 特征码失效
L4N 修改了 `client.dll` 的部分代码，导致本项目某些 pattern 扫描失败。**已实证的案例**：

```cpp
// Offsets.cpp:17 — CParticleSystemMgr 的 pattern
U::Pattern.Find("client.dll", "0C 8B 0D ? ? ? ? 52 50 E8 82 5F")
// 在 L4N 修改的 client.dll 上返回 0（扫描失败）
```

日志证据：
```
Step 3.5: Offsets check: m_dwCParticleSystemMgr=00000000
Step 3.6: dereferencing offsets for ClientMode/GlobalVars/ParticleSystemMgr
!!! SEH exception 0xC0000005 in ModuleEntry::Load
```

`m_dwCParticleSystemMgr=0` 时，`**reinterpret_cast<void***>(0)` 触发访问违规。

### 5.2 应对措施
每个偏移解引用前判空（见 [Entry.cpp](necola/hook/Entry.cpp) Step 3.6）：

```cpp
if (U::Offsets.m_dwClientMode) {
    I::ClientMode = **reinterpret_cast<void***>(U::Offsets.m_dwClientMode);
}
if (U::Offsets.m_dwGlobalVars) {
    I::GlobalVars = **reinterpret_cast<CGlobalVarsBase***>(U::Offsets.m_dwGlobalVars);
}
if (U::Offsets.m_dwCParticleSystemMgr) {
    I::ParticleSystemMgr = **reinterpret_cast<void***>(U::Offsets.m_dwCParticleSystemMgr);
}
```

ADS 功能不强依赖 `ParticleSystemMgr`，null 时跳过即可。

### 5.3 接口名兼容性
本项目使用的接口名（见 [Entry.cpp](necola/hook/Entry.cpp)）：

| 模块 | 接口名 | 用途 |
|---|---|---|
| client.dll | VClient016 | IBaseClientDLL |
| client.dll | VClientEntityList003 | IClientEntityList |
| client.dll | VClientPrediction001 | IPrediction |
| engine.dll | VModelInfoClient004 | IVModelInfo |
| engine.dll | GAMEEVENTSMANAGER002 | IGameEventManager2 |
| engine.dll | VEngineVGui001 | IEngineVGui |
| engine.dll | VEngineClient013 | IVEngineClient |
| engine.dll | IEngineSoundClient003 | IEngineSound |
| engine.dll | VEngineClientStringTable001 | INetworkStringTableContainer |
| engine.dll | EngineTraceClient003 | IEngineTrace |
| datacache.dll | MDLCache004 | IMDLCache |
| filesystem_stdio.dll | VFileSystem018 | IFileSystem |
| vgui2.dll | VGUI_Panel009 | IVGuiPanel |
| vgui2.dll | VGUI_Surface031 | IVGuiSurface |
| vguimatsurface.dll | VGUI_Surface031 | IMatSystemSurface |
| inputsystem.dll | InputSystemVersion001 | IInputSystem |

**适配新版本时**：这些接口名可能变化（数字递增），需用 IDA/Ghidra 检查引擎 DLL 的 `CreateInterface` 导出表。

## 六、无法逆向的部分（诚实记录）

以下因条件限制未做二进制级逆向，结论基于行为观测和模板证据：

1. **L4N 主平台 DLL**：不在提取包中（只有模板/工具/shader），无法反汇编。config.vdf 机制基于模板注释推断。
2. **D3D 回调时机**：`OnD3DCreated`/`OnD3DDeviceCreated` 因用户使用 `-vulkan`（dxvk），未实际触发，未研究。
3. **`custom_commands` 对第三方命令的行为**：未实测，推断为"可能不可靠"。

如需深入，获取 L4N 主 DLL（通常在 `left4dead2/bin/` 或作为 ASI 注入）后用 IDA Pro 分析 `GetL4NPluginInstance` 的调用栈。
