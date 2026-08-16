# 架构文档

> 本文档描述 Necola 的代码架构、模块依赖关系、初始化流程、Source 引擎接口使用方式。

## 一、整体架构

```
┌─────────────────────────────────────────────────────┐
│  left4dead2.exe (游戏进程)                          │
│  ┌───────────────────────────────────────────────┐  │
│  │ L4N 平台 (修改版 left4dead2.exe,非注入)      │  │
│  │  - 扫描 neko/plugins/*.dll                    │  │
│  │  - 调用 GetL4NPluginInstance                  │  │
│  │  - 回调 OnModuleLoaded / OnGameLaunch         │  │
│  └───────────────────┬───────────────────────────┘  │
│                      │ LoadLibraryExA + GetProcAddress │
│  ┌───────────────────▼───────────────────────────┐  │
│  │ necola_ads.dll (本插件)                       │  │
│  │  ┌─────────────┐  ┌──────────────────────┐    │  │
│  │  │ dllmain.cpp │  │ hook/Entry.cpp       │    │  │
│  │  │ L4N入口     ├──▶│ 初始化主流程         │    │  │
│  │  │ 模块等待    │  └──────────┬───────────┘    │  │
│  │  └─────────────┘             │                │  │
│  │  ┌───────────────────────────▼────────────┐  │  │
│  │  │ Feature/ (功能层)                       │  │  │
│  │  │  ├ AdsSupport/   ADS状态机+动画重映射   │  │  │
│  │  │  ├ SequenceModify/ 序列修正(ADS底层)    │  │  │
│  │  │  ├ MenuManager/   游戏内菜单UI          │  │  │
│  │  │  ├ CommandManager/ 控制台命令           │  │  │
│  │  │  ├ InputManager/  输入处理              │  │  │
│  │  │  └ BodygroupFix/  bodygroup修复         │  │  │
│  │  └────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────┐  │  │
│  │  │ Raw/ (Hook层,按被Hook类组织)            │  │  │
│  │  │  ├ BaseClient/   IBaseClientDLL钩子     │  │  │
│  │  │  ├ BaseAnimating/ 动画序列钩子         │  │  │
│  │  │  ├ BaseCombatWeapon/ 武器动画钩子       │  │  │
│  │  │  ├ EngineVGui/   Paint绘制钩子          │  │  │
│  │  │  └ GameEventManager/ 事件钩子           │  │  │
│  │  └────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────┐  │  │
│  │  │ sdk/ (引擎SDK)                          │  │  │
│  │  │  ├ l4d2/interfaces/ 接口抽象类          │  │  │
│  │  │  ├ l4d2/entities/ 实体类               │  │  │
│  │  │  ├ utils/ Hook/Pattern/Interface工具    │  │  │
│  │  │  └ Offsets.cpp 特征码扫描              │  │  │
│  │  └────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────┘  │
│  ┌───────────────────────────────────────────────┐  │
│  │ Source 引擎模块 (L4N加载)                    │  │
│  │  client.dll, engine.dll, vgui2.dll, ...      │  │
│  └───────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
```

## 二、模块依赖关系

### 2.1 依赖方向（上层依赖下层）
```
dllmain.cpp (L4N入口)
    │
    ▼
Entry.cpp (初始化编排)
    │
    ├─► sdk/ (接口获取、偏移扫描)
    │
    ├─► Feature/ (功能模块)
    │       │
    │       ├─► AdsSupport 依赖 SequenceModify
    │       │             依赖 sdk (实体、接口)
    │       │
    │       ├─► SequenceModify 依赖 sdk (NetVar)
    │       │
    │       ├─► MenuManager 依赖 AdsSupport (配置项)
    │       │                依赖 FeatureConfigManager
    │       │
    │       └─► CommandManager (无依赖,纯注册)
    │
    └─► Raw/ (Hook点)
            │
            └─► 调用 Feature/ 的逻辑
```

### 2.2 关键依赖说明

**AdsSupport ↔ SequenceModify**：
- `AdsSupport` 是上层，实现 ADS 状态机和开镜动画选择
- `SequenceModify` 是底层，钩取网络序列属性（`m_nLayerSequence`、`m_nAnimationParity`），供 ADS 层拦截/重映射
- ADS 依赖 SequenceModify 的 hook 已安装才能工作

**MenuManager → AdsSupport**：
- 菜单的配置项（开关、武器选择）直接读写 `AdsSupport` 的配置变量（`G::Vars.ads*`）
- 菜单切换"启用ADS"开关会调用 `F::AdsMgr.Init()` 或 `ForceExitADS()`

**Raw/ → Feature/**：
- `Raw/BaseAnimating` 的 `SelectWeightedSequence` 钩子调用 `AdsSupport` 的重映射逻辑
- `Raw/BaseCombatWeapon` 的 `SendWeaponAnim` 钩子调用 `AdsSupport` 的动画拦截
- `Raw/EngineVGui` 的 `Paint` 钩子调用 `MenuManager.Draw()` 和准星隐藏逻辑

## 三、初始化流程

### 3.1 完整时序

```
游戏启动
  │
  ▼
L4N 加载 necola_ads.dll
  │
  ▼ DllMain(DLL_PROCESS_ATTACH)
  │   - 无实质操作（日志可选）
  │
  ▼ GetL4NPluginInstance()
  │   - 返回全局 NecolaL4NPlugin 实例
  │
  ▼ OnGameLaunch() 或 OnModuleLoaded() (先到者)
  │   - std::call_once 保证只触发一次
  │   - CreateThread(InitThreadFunc)
  │
  ▼ InitThreadFunc
  │   │
  │   ▼ 等待模块就绪
  │   │   - 条件变量等待 7 个关键模块
  │   │   - L4N 回调 notify 唤醒
  │   │   - 100ms 兜底轮询
  │   │
  │   ▼ Hook_necola()
  │       │
  │       ▼ LoadIni() — 加载 kpatch.ini
  │       │
  │       ▼ CGlobal_ModuleEntry::Load()  ← 核心
  │           │
  │           ▼ Step 1: U::Offsets.Init()
  │           │   - 特征码扫描 client.dll/datacache.dll
  │           │   - 定位 ClientMode、GlobalVars 等指针
  │           │
  │           ▼ Step 2: G::Vars.Load()
  │           │   - 加载默认配置
  │           │
  │           ▼ Step 3: CreateInterface 获取引擎接口
  │           │   - 16 个接口（client/engine/datacache/...）
  │           │
  │           ▼ Step 3.6: 解引用偏移指针
  │           │   - ClientMode = **(offset)  (判空)
  │           │   - GlobalVars = **(offset)  (判空)
  │           │   - ParticleSystemMgr = **(offset)  (判空)
  │           │
  │           ▼ Step 4: G::InputManagerI.Init()
  │           │
  │           ▼ Step 5: G::Hooks.Init()
  │           │   - 安装所有 MinHook 钩子
  │           │
  │           ▼ Step 6: 加载持久化配置
  │           │   - FeatureConfig.json → AdsSupport.LoadConfig
  │           │
  │           ▼ Step 7: F::SModify.RecvPropDataHook()
  │           │   - 安装网络属性代理钩子
  │           │
  │           ▼ Step 8: AdsSupport.Init() (若启用)
  │           │
  │           ▼ Step 9: 菜单字体+配置同步
  │           │   - InitMenuFonts()
  │           │   - InitConfigSwitches()
  │           │
  │           ▼ Step 10: 注册控制台命令
  │           │   - necola_menu, necola_ads, ...
  │           │
  │           ▼ Step 11: KeyBinds 自动绑定
  │               - 读 FeatureConfig.json 的 KeyBinds
  │               - 执行 bind 命令
  │
  ▼ 初始化完成，插件进入运行态
  │
  ▼ 游戏运行中（Hook 被触发）
  │   - Paint → 菜单绘制 + 准星隐藏
  │   - SelectWeightedSequence → ADS 动画重映射
  │   - SendWeaponAnim → ADS 动画拦截
  │   - RecvProxySequence → 序列修正
  │
  ▼ 游戏退出
      - DllMain(DLL_PROCESS_DETACH)
      - Undo_necola: G::Hooks.undo() + InputManagerI.undo()
```

### 3.2 SEH 安全网
`CGlobal_ModuleEntry::Load()` 用 `__try/__except` 包裹整个初始化体：

```cpp
void CGlobal_ModuleEntry::Load() {
    __try {
        RunLoadBody();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // 捕获 SEH 异常（如访问违规），避免进程静默终止
    }
}
```

**原因**：C++ 的 `catch(...)` 配合 `/EHsc` **无法捕获** SEH 异常（如空指针解引用的 0xC0000005）。不用 `__try/__except` 的话，一个失败的偏移解引用会让整个游戏进程崩溃，且无日志。

## 四、Source 引擎接口使用

### 4.1 接口获取
通过 `CreateInterface` 工厂函数，封装在 [sdk/Interface.cpp](necola/sdk/Interface.cpp)：

```cpp
I::EngineClient = U::Interface.Get<IVEngineClient*>("engine.dll", "VEngineClient013");
```

内部流程：
1. `GetModuleHandleA("engine.dll")` 获取模块基址
2. `GetProcAddress(hModule, "CreateInterface")` 获取工厂函数
3. 调用 `CreateInterface("VEngineClient013", &returnCode)` 返回接口指针

### 4.2 关键接口用途

| 接口 | 用途 | 使用场景 |
|---|---|---|
| `IBaseClientDLL` | 客户端主接口 | LevelInit/ClientActive hook |
| `IVEngineClient` | 引擎客户端 | `IsInGame()`、`ClientCmd()`、`GetLocalPlayer()` |
| `IClientEntityList` | 实体列表 | 获取本地玩家、武器实体 |
| `IEngineVGui` | VGUI 引擎 | Paint hook（菜单绘制） |
| `IMatSystemSurface` | 表面绘制 | 菜单矩形/文本/字体 |
| `IVModelInfo` | 模型信息 | 动画序列查找 |
| `IMDLCache` | 模型缓存 | GetStudioHdr |
| `CGlobalVarsBase` | 全局变量 | `curtime`（菜单闪烁计时） |
| `ClientMode` | 客户端模式 | ViewRender 等 |

### 4.3 特征码扫描的指针
部分关键数据无接口暴露，需 pattern scan 定位：

| 偏移 | Pattern | 解引用得到 |
|---|---|---|
| `m_dwClientMode` | `89 04 B5 ? ? ? ? E8` | `ClientMode*` |
| `m_dwGlobalVars` | `A1 ? ? ? ? D9 40 0C 51 D9 1C 24 57` | `CGlobalVarsBase*` |
| `m_dwCParticleSystemMgr` | `0C 8B 0D ? ? ? ? 52 50 E8 82 5F` | `ParticleSystemMgr*`（L4N 上可能失效） |

解引用模式：`I::ClientMode = **reinterpret_cast<void***>(offset)`（双重间接：pattern 指向存放指针的地址，再取值得到实际对象）。

## 五、Hook 机制

### 5.1 MinHook 封装
见 [sdk/utils/Hook.h](necola/sdk/utils/Hook.h)，三个类：

**CFunction**：钩普通函数
```cpp
Hook::CFunction func;
func.Init(pTarget, pDetour);  // 创建钩子
auto original = func.Original<FuncType>();  // 调用原函数
```

**CTable**：钩虚函数表项
```cpp
Hook::CTable table;
table.Init(pVTable);  // 探测表大小
table.Hook(pDetour, index);  // 钩第 index 个虚函数
auto original = table.Original<FuncType>(index);
```

**CVMTable**：虚函数表替换（整体复制表，修改副本）
- 比 CTable 更安全（不直接改原表），但开销略大

### 5.2 Hook 注册
见 [hook/Hooks.cpp](necola/hook/Hooks.cpp)。所有 hook 在 `G::Hooks.Init()` 时统一安装，在 `undo()` 时统一卸载。

### 5.3 典型 Hook 示例（EngineVGui::Paint）
[necola/hook/Raw/EngineVGui/EngineVGui.cpp](necola/hook/Raw/EngineVGui/EngineVGui.cpp)：

```cpp
void __fastcall EngineVGui::Paint::Detour(void* ecx, void* edx, int mode) {
    Table.Original<FN>(Index)(ecx, edx, mode);  // 先调用原函数

    if (mode == PAINT_INGAMEPANELS) {
        // 1. ADS 准星隐藏逻辑
        if (G::Vars.enableAdsSupport && G::Vars.adsHideCrosshairMode > 0) {
            // 状态切换时执行 crosshair 0/1
        }
        // 2. 菜单绘制
        F::MenuMgr.Draw();
    }
}
```

## 六、配置系统

### 6.1 双配置文件
- **`kpatch.ini`**（INI格式）：旧版兼容，[System] 段含 `cmdline` 等
- **`neko/FeatureConfig.json`**（JSON）：主配置，ADS/序列修正/按键绑定

### 6.2 FeatureConfigManager
见 [sdk/utils/FeatureConfigManager.h](necola/sdk/utils/FeatureConfigManager.h)，提供 `LoadConfig()` / `SaveConfig(doc)` 读写 JSON。

### 6.3 配置加载流程
1. `Entry.cpp` Step 6 调用 `AdsMgr.LoadConfig(doc)` 读 ADS 配置到 `G::Vars`
2. 菜单切换开关时，更新 `G::Vars` 并 `SaveConfig(doc)` 写回 JSON
3. `KeyBinds` 数组在初始化末尾读取，执行 `bind` 命令

## 七、日志系统

### 7.1 spdlog 配置
见 [dllmain.cpp](necola/dllmain.cpp) `InitSpdlog()`：
- 日志文件：`<left4dead2>/L4N-Necola-ADS.log`
- 级别：`info`
- flush 策略：`flush_on(warn)`（warn 及以上立即刷盘）

### 7.2 日志开关
两个**功能日志开关**（默认关，开启后在动画事件级打日志）：
- `G::Vars.adsLog`：ADS 相关日志
- `G::Vars.sequenceLog`：序列修正日志

**警告**：这两个开关开着时，会在高频动画钩子（`SelectWeightedSequence`、`RecvProxySequence`）里同步写日志，可能导致卡顿。排查问题时临时开启，确认后关闭。

### 7.3 历史教训
曾有版本用 `flush_on(trace)` 级别 + 每帧打日志，导致明显卡顿。现版本已改为 `info` + `flush_on(warn)`，且动画级日志门控由开关控制。
