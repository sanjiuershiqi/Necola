# Necola 项目交接手册

> 本文档面向接手本项目的下一位开发者（人或 AI）。读完本文 + 四份分文档，即可独立理解、构建、调试、扩展本插件。

## 一、这是什么

Necola 是一个 **L4N (Left 4 Neko) 平台的附属插件**，为 Left 4 Dead 2 提供 **ADS（Aim Down Sights，开镜瞄准）** 功能。它不是独立注入的 DLL，而是被 L4N 主平台加载的插件。

**一句话定位**：L4N 负责注入游戏进程，Necola 作为 L4N 插件被加载，在 Source 引擎模块就绪后通过 MinHook 钩取关键函数，实现武器开镜动画、准星隐藏、序列修正等功能。

**当前版本**：1.4.0（见 [necola/vars.h](necola/vars.h) 的 `sFixVer`）

**构建产物**：`build/windows/x86/release/necola_ads.dll`（约 600KB）

**部署位置**：`<left4dead2>/neko/plugins/necola_ads.dll`

## 二、阅读顺序（重要）

| 序号 | 文档 | 内容 |
|---|---|---|
| 1 | **本文 (PROJECT_HANDBOOK.md)** | 项目总纲、文件地图、核心概念、接手清单 |
| 2 | [docs/L4N_PLUGIN_RESEARCH.md](docs/L4N_PLUGIN_RESEARCH.md) | L4N 逆向分析全过程、插件接口契约、模块加载机制 |
| 3 | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | 代码架构、模块依赖、初始化流程、Source 引擎接口 |
| 4 | [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | 构建、调试、日志、部署、故障排查 |

**接手者必读顺序**：本文 → L4N_PLUGIN_RESEARCH → ARCHITECTURE → DEVELOPMENT。

## 三、文件地图

### 根目录
```
/workspace
├── xmake.lua                    # 构建配置（xmake，非 CMake）
├── justfile                     # 本地构建/安装/发布脚本（just 命令）
├── kpatch.ini                   # 旧版配置模板（兼容保留）
├── README.md                    # 用户向说明
├── PROJECT_HANDBOOK.md          # 本文档
├── docs/                        # 详细文档（见下）
└── .github/workflows/build.yml  # GitHub Actions CI/CD
```

### 源码目录 `necola/`
```
necola/
├── dllmain.cpp                  # DLL入口 + L4N插件实例 + 初始化线程
├── l4n_plugin.h                 # L4N插件接口定义（逆向所得，见L4N研究文档）
├── vars.h / Vars.cpp / Vars.h   # 全局配置变量
├── std.h                        # 预编译头集合
│
├── hook/
│   ├── Entry.cpp / Entry.h       # 核心初始化（接口获取、Hook安装、命令注册）
│   ├── Hooks.cpp / Hooks.h       # Hook总表
│   ├── Vars.h                    # 全局变量（CGlobal_Vars）
│   │
│   ├── Feature/                 # 功能模块
│   │   ├── AdsSupport/           # ADS核心：开镜状态机、动画重映射
│   │   ├── SequenceModify/       # 序列修正（ADS依赖的底层）
│   │   ├── MenuManager/          # 游戏内菜单UI
│   │   ├── CommandManager/       # 控制台命令注册
│   │   ├── InputManager/         # 输入管理
│   │   └── BodygroupFix/         # Bodygroup修复
│   │
│   └── Raw/                      # 原始Hook点（按被Hook类组织）
│       ├── BaseClient/           # IBaseClientDLL钩子
│       ├── BaseAnimating/        # CBaseAnimating钩子
│       ├── BaseCombatWeapon/     # CBaseCombatWeapon钩子
│       ├── EngineVGui/           # Paint绘制钩子（菜单+准星）
│       └── GameEventManager/     # 游戏事件钩子
│
├── sdk/                          # Source引擎SDK
│   ├── SDK.h                     # 统一头
│   ├── Offsets.cpp               # 特征码扫描（pattern scan）
│   ├── Pattern.cpp               # Pattern扫描实现
│   ├── Interface.cpp              # CreateInterface封装
│   ├── VFunc.cpp                 # 虚函数表工具
│   ├── NetVarManager.cpp          # 网络变量管理
│   └── l4d2/                     # L4D2专用SDK
│       ├── interfaces/           # 引擎接口抽象类
│       │   ├── BaseClientDLL.h, EngineClient.h, EngineVGui.h ...
│       │   └── MatSystemSurface.h # 菜单绘制依赖
│       ├── entities/             # 实体类
│       │   ├── C_TerrorPlayer.h, C_TerrorWeapon.h ...
│       │   └── C_BaseAnimating.h
│       └── includes/             # 基础类型（Vector, Color, usercmd等）
│
├── sdk/utils/                    # SDK工具
│   ├── Hook.h                    # MinHook封装（CFunction/CTable/CVMTable）
│   ├── Offsets.h, Pattern.h, Interface.h, VFunc.h
│   └── FeatureConfigManager.h    # JSON配置读写
│
└── libs/                         # 第三方库
    ├── MinHook/                  # 函数Hook库
    ├── fnv.h                     # FNV哈希
    └── xorstr.h                  # 字符串混淆
```

### 关键文件职责速查

| 文件 | 一句话职责 | 改动风险 |
|---|---|---|
| [necola/dllmain.cpp](necola/dllmain.cpp) | L4N插件入口、初始化线程、模块就绪等待 | 高（改错导致加载失败） |
| [necola/hook/Entry.cpp](necola/hook/Entry.cpp) | 初始化主流程：接口→偏移→Hook→命令 | 高（崩溃多发地） |
| [necola/hook/Feature/AdsSupport/AdsSupport.cpp](necola/hook/Feature/AdsSupport/AdsSupport.cpp) | ADS状态机、动画重映射核心逻辑 | 中（功能行为） |
| [necola/hook/Feature/MenuManager/MenuManager.h](necola/hook/Feature/MenuManager/MenuManager.h) | 游戏内菜单UI（约890行） | 低（纯UI） |
| [necola/sdk/Offsets.cpp](necola/sdk/Offsets.cpp) | 特征码扫描（适配L4N修改的引擎） | 中（引擎更新会失效） |
| [necola/l4n_plugin.h](necola/l4n_plugin.h) | L4N插件接口契约 | 低（接口稳定） |

## 四、核心概念

### 4.1 L4N 插件机制
L4N 是 L4D2 的修改平台（类似 CS 的 CS:GO 帝王）。它在游戏启动时扫描 `<left4dead2>/neko/plugins/*.dll`，对每个 DLL：
1. `LoadLibraryExA` 加载
2. `GetProcAddress("GetL4NPluginInstance")` 获取工厂函数
3. 调用获得 `IL4NPlugin*` 实例
4. 在游戏生命周期中调用虚函数回调：`OnGameLaunch`、`OnModuleLoaded`、`OnD3D*`

详见 [docs/L4N_PLUGIN_RESEARCH.md](docs/L4N_PLUGIN_RESEARCH.md)。

### 4.2 模块就绪等待
Source 引擎在启动时分阶段加载多个 DLL（client.dll、engine.dll、vgui2.dll 等）。Necola 需要等关键模块加载完成才能调用 `CreateInterface` 获取引擎接口。

**机制**：条件变量 + L4N 回调事件驱动 + `GetModuleHandleA` 兜底轮询。详见 L4N 研究文档第 3 节。

### 4.3 Source 引擎接口
通过 `CreateInterface(modname, interfaceName)` 获取引擎接口指针。例如：
```cpp
I::EngineClient = U::Interface.Get<IVEngineClient*>("engine.dll", "VEngineClient013");
```
接口名（如 `VEngineClient013`）是引擎内部版本号，随游戏更新可能变化。本项目适配 L4N 修改后的 L4D2 引擎。

### 4.4 特征码扫描（Pattern Scan）
引擎内部数据/函数无导出符号，通过字节模式匹配定位。例如：
```cpp
U::Pattern.Find("client.dll", "89 04 B5 ? ? ? ? E8")  // 定位 ClientMode 指针
```
`?` 为通配符。L4N 修改引擎后部分 pattern 可能失效（本项目已遇到并处理）。

### 4.5 MinHook 钩子
使用 MinHook 库钩取引擎函数。两种模式：
- **CTable**：钩虚函数表项（如 `Paint`、`LevelInit`）
- **CFunction**：钩普通函数（如 `SelectWeightedSequence`）

封装见 [necola/sdk/utils/Hook.h](necola/sdk/utils/Hook.h)。

### 4.6 ADS 功能概述
ADS（Aim Down Sights）实现武器开镜瞄准：
- 拦截武器动画序列（`SelectWeightedSequence`、`RecvProxySequence`）
- 根据当前 ADS 状态重映射动画序列到开镜版本
- 支持三种状态：ADS（纯开镜）、MIXED（混合）、NONE（普通）
- 支持每武器配置准星隐藏、开镜模式

## 五、环境要求

| 项 | 要求 |
|---|---|
| 操作系统 | Windows（目标），Linux/macOS 仅能做代码编辑 |
| 编译器 | MSVC（xmake 自动选择） |
| 构建工具 | xmake（非 CMake）+ just（可选，便捷命令） |
| 依赖 | spdlog（日志）、minhook（Hook）、inipp（INI解析）、nlohmann/json（配置） |
| 目标架构 | x86（32位，L4D2 是32位进程） |
| 游戏版本 | Left 4 Dead 2（Steam，配合 L4N v2.41.0+） |

## 六、快速接手清单

新接手者按以下步骤可在一小时内跑通：

1. **克隆仓库**
   ```bash
   git clone <repo-url>
   cd Necola
   ```

2. **装依赖**（Windows）
   - xmake：https://xmake.io
   - just（可选）：https://github.com/casey/just

3. **构建**
   ```bash
   xmake f -m release -p windows -a x86
   xmake
   ```
   或用 just：`just build`

4. **部署**
   - 把 `build/windows/x86/release/necola_ads.dll` 复制到 `<L4D2>/neko/plugins/`
   - 确认 L4N 已安装（`<L4D2>/neko/` 目录存在）

5. **验证**
   - 启动游戏，检查 `<L4D2>/L4N-Necola-ADS.log` 是否生成
   - 游戏内控制台执行 `necola_menu` 打开菜单

6. **阅读文档**
   - 改代码前先读 [ARCHITECTURE.md](docs/ARCHITECTURE.md)
   - 遇问题先查 [DEVELOPMENT.md](docs/DEVELOPMENT.md) 故障排查章节

## 七、历史踩坑记录（必读）

以下问题是项目演进中实际遇到的，新接手者极易重蹈覆辙：

1. **多线程并发初始化** → 用 `std::once_flag` 保证单线程
2. **SEH异常静默崩溃** → `__try/__except` 包裹 + null偏移守卫
3. **特征码在L4N引擎失效** → 每个偏移解引用前判空
4. **日志开关误开导致卡顿** → 动画事件级日志默认关
5. **`xmake.lua` glob 不递归** → 用 `**.cpp` 而非 `*.cpp`
6. **CI 在非默认分支不触发** → 必须合并到 main

详见 [DEVELOPMENT.md](docs/DEVELOPMENT.md) 第 5 节"故障排查"。

## 八、如何扩展

### 新增一个控制台命令
在 [Entry.cpp](necola/hook/Entry.cpp) 的命令注册段添加：
```cpp
F::CmdMgr.RegistCommand("necola_xxx", [](int*) {
    // 逻辑
}, "描述");
```

### 新增一个引擎Hook
1. 在 [sdk/utils/Hook.h](necola/sdk/utils/Hook.h) 理解 CTable/CFunction
2. 在 `hook/Raw/<对应类>/` 新建 .h/.cpp
3. 在 `hook/Hooks.cpp` 注册
4. 在 `Entry.cpp` 调用 `Init()`

### 适配新引擎版本
- 重新扫描 [Offsets.cpp](necola/sdk/Offsets.cpp) 里的 pattern
- 验证接口名（如 `VEngineClient013` → `VEngineClient014`？）
- 详见 [L4N_PLUGIN_RESEARCH.md](docs/L4N_PLUGIN_RESEARCH.md) 第 5 节

## 九、联系与许可

- 仓库：https://github.com/sanjiuershiqi/Necola
- 构建状态：见 GitHub Actions
- 本项目为 L4N 附属插件，不分发 L4N 本体
