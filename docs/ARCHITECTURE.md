# Necola 架构文档

> 本文描述当前源码实际行为。L4N 宿主内部的未公开实现见
> [L4N_PLUGIN_RESEARCH.md](L4N_PLUGIN_RESEARCH.md)，不要把推断当成接口契约。

## 一、系统边界

Necola 是一个 x86 原生 DLL。官方 L4N SDK 示例把插件放在游戏根目录的
`bin/neko/plugins/`，导出 `GetL4NPluginInstance` 并返回 `IL4NPlugin*`。

```text
L4N 宿主
  -> GetL4NPluginInstance
  -> OnGameLaunch / OnModuleLoaded / D3D callbacks
       |
       v
necola/dllmain.cpp
  -> 单次初始化线程
  -> 模块轮询
  -> hook/Entry.cpp
       |
       +-> sdk/: Source 接口、pattern、netvar
       +-> hook/Raw/: 原始 Hook 点
       +-> hook/Feature/: ADS、战役计时、击杀反馈、序列、菜单、输入、bodygroup、命令
```

L4N 提供加载入口和生命周期回调。Necola 的主要运行逻辑自行使用 Source `CreateInterface`、
MinHook、RecvProp proxy 和窗口过程替换实现；同时通过 `ICvar` 读取少量 `l4n_*` cvar 进行协调。

两个 D3D 回调目前为空。Necola 不通过 D3D 绘制菜单，也没有自定义 shader 渲染路径。

## 二、入口与初始化

### 2.1 插件入口

[`necola/dllmain.cpp`](../necola/dllmain.cpp) 中：

- `GetL4NPluginInstance()` 返回函数内静态 `NecolaL4NPlugin`。
- `GetInterfaceVersion()` 返回 1。
- 名称为 `Necola-ADS`，版本字符串来自 `sFixVer`。
- `OnModuleLoaded()` 把模块名/句柄存入 `L4N::Env`。
- `OnGameLaunch()` 和 `OnModuleLoaded()` 共用一个原子启动标志，先到者尝试创建初始化线程；创建失败会复位标志，允许后续回调重试。

这里没有“只等 client 回调”或“等待 serverbrowser”的分支。

### 2.2 模块等待

初始化线程每 1 秒调用 `GetModuleHandleA` 检查以下模块，最多循环 120 次：

```text
client.dll
engine.dll
vgui2.dll
datacache.dll
vguimatsurface.dll
inputsystem.dll
filesystem_stdio.dll
vstdlib.dll
materialsystem.dll
```

当前实现没有条件变量或回调唤醒。等待超时会记录错误并终止线程，不会进入 `Hook_necola()`。

`L4N::Env` 会缓存回调句柄，不过 `U::Interface` 和 `U::Pattern` 当前仍直接调用
`GetModuleHandleA`，没有使用该缓存。

### 2.3 初始化步骤

`CGlobal_ModuleEntry::Load()` 用 SEH 包住 `RunLoadBody()`，实际顺序如下：

1. `U::Offsets.Init()` 扫描硬编码字节 pattern。
2. `G::Vars.Load()` 从相对路径 `kpatch.ini` 读取 `[AdsSupport] enableAdsSupport`。
3. 通过 `CreateInterface` 获取 18 个接口。
4. 集中验证当前功能必需的接口和 pattern；缺失时终止。
5. `L4N::Env.Init()` 探测并缓存相关 `l4n_*` cvar。
6. 只解引用当前运行路径必需的 GlobalVars 指针，并验证结果。
7. 替换 Valve 窗口过程，收集键鼠消息。
8. 初始化 MinHook 并安装 Raw hook，检查每一步返回值。
9. 从 `necola\FeatureConfig.json` 读取 ADS、序列和菜单外观配置。
10. 找齐并替换 3 个 `CBaseViewModel` RecvProp proxy。
11. 按配置初始化 ADS，初始化菜单字体和菜单开关。
12. 注册 7 个 `necola_*` 控制台命令。

不存在 `FeatureConfig.json/KeyBinds` 自动读取或自动 `bind`。

初始化返回失败或触发 SEH 后会恢复已安装的 RecvProp proxy、MinHook 和窗口过程。控制台命令仍
没有注销机制，因此清理尚不是完全事务性的。

## 三、Source 接口

当前获取 18 个接口：

| 模块 | 接口 |
|---|---|
| `client.dll` | `IBaseClientDLL`、`IClientEntityList`、`IPrediction` |
| `engine.dll` | `IVModelInfo`、`IGameEventManager2`、`IEngineVGui`、`IVEngineClient`、`IEngineSound`、`INetworkStringTableContainer`、`IEngineTrace` |
| `datacache.dll` | `IMDLCache` |
| `filesystem_stdio.dll` | `IFileSystem` |
| `vgui2.dll` | `IVGuiPanel`、`IVGuiSurface` |
| `vguimatsurface.dll` | `IMatSystemSurface` |
| `inputsystem.dll` | `IInputSystem` |
| `vstdlib.dll` | `ICvar` |
| `materialsystem.dll` | `IMaterialSystem` |

接口名称和版本均为硬编码。当前代码会在 Hook 安装前验证 ADS、菜单和活动 Hook 路径必需的接口；
`ICvar` 缺失会降级 L4N 协调与直接准星控制，`IMaterialSystem` 缺失只会停用战役计时 HUD 接管。

Pattern scan 用于定位非导出函数和全局指针。Pattern 失败表示当前二进制与签名不匹配，原因可能
是游戏、L4N 或其他二进制版本变化，不能仅凭失败确定是哪一方修改了 DLL。

## 四、Hook 拓扑

`G::Hooks.Init()` 注册 5 个 Raw hook 组，实际包含 12 个 MinHook detour：

| 组 | Hook 点 | 用途 |
|---|---|---|
| BaseClient | `LevelInitPreEntity`、`LevelInitPostEntity`、`LevelShutdown`、`FrameStageNotify`、`IN_KeyEvent` | 关卡计时生命周期、帧更新、菜单按键、ADS 输入 |
| EngineVGui | `Paint` | 战役计时材质、击杀反馈、准星控制和菜单绘制 |
| BaseCombatWeapon | `SendWeaponAnim`、`SetIdealActivity` | 武器动画拦截 |
| BaseAnimating | `RecvProxySequenceViewModel`、`SelectWeightedSequence`、`FireEvent` | viewmodel 序列与动画事件 |
| GameEventManager | `FireEventClient` | ADS 清理、战役计时和击杀反馈事件分发 |

`SequenceModify::RecvPropDataHook()` 另外替换 3 个网络属性代理：

- `m_nLayerSequence`
- `m_nAnimationParity`
- `m_nNewSequenceParity`

这些 proxy 不属于 MinHook。`m_dwRecvProxySequence` 虽然会被 pattern 定位，但当前没有安装为
detour，文档和排障时不要把它列为已启用 Hook。

当前 Hook 初始化检查 MinHook 初始化、12 个 Hook 创建及统一启用结果。`CTable` 使用调用方给出的
最小表大小，不再扫描未知长度虚表；任一步失败都会禁用并反初始化 MinHook。

## 五、ADS 状态与数据流

ADS 有 5 个深度状态：

```text
ADS_NONE -> ADS_LEVEL1 -> ADS_LEVEL2 -> ADS_LEVEL3 -> ADS_LEVEL4 -> ADS_NONE
```

缺失动画的层级会被跳过。`m_isMixed` 是可与任一深度状态组合的独立布尔状态，不是第三个互斥
枚举值。系统还保存上一组 `(AdsState, Mixed)` 用于回退。

主要运行路径：

```text
控制台命令 / +zoom / +use
  -> 校验连接、玩家、武器和攻击状态
  -> 扫描并缓存当前 viewmodel 的 activity/sequence
  -> 选择进入、退出或层级切换动画
  -> 更新 AdsState/Mixed 和 0.4 秒软件锁
  -> FRAME_RENDER_START 持续维护 m_nSequence
```

`SelectWeightedSequence`、武器动画 Hook 和 RecvProp proxy 会把普通 activity/sequence 映射到
当前 ADS/MIXED 对应序列。武器变化、双持变化、玩家死亡和部分游戏事件会静默复位状态。

代码中的 GPU fence、ray tracing、deferred pass 等注释不对应真实 GPU 资源或 D3D 调用。实际
行为是 Source viewmodel 动画、序列字段和 bodygroup 操作。

## 六、战役计时器

`CampaignTimer` 使用 `I::GlobalVars->curtime` 计算当前小关时间，因此地图加载期间不会推进。已完成小关
在 `map_transition` 时提交到战役累计；`mission_lost` 丢弃当前小关失败耗时，并在 `round_start` 或
同图重新初始化后重新开始。没有过渡事件的新地图会开始新的战役累计。

配套 `cs2hud444` 的六个数字 VMT 不再使用全局 `CurrentTime` proxy。`EngineVGui::Paint` 在原 HUD
绘制前通过 `IMaterialSystem::FindMaterial()` 获取指定材质，并把时、分、秒六位数字写入各自的
`$frame`。材质引用仅在关卡运行期间持有，不会替换全局材质代理；未安装配套 HUD 时每关只尝试解析
一次并静默停用显示接管。

## 七、菜单、准星和 L4N 协调

`EngineVGui::Paint` 在原函数绘制 HUD 前更新战役计时材质；原函数返回后，在
`PAINT_INGAMEPANELS`：

1. 根据 ADS 配置和当前武器判断是否隐藏准星。
2. 检查 `L4N::Env.HudVisible()`，L4N 隐藏 HUD 时不与其争用准星。
3. 优先直接写 `crosshair` 根 ConVar，保存并恢复用户原值。
4. 只有找不到 ConVar 时才回退到 `ClientCmd("crosshair 0/1")`。
5. 绘制 Necola VGUI 菜单。

菜单每帧从 `IMatSystemSurface::GetScreenSize()` 更新布局，支持左/中/右锚点、三档背景透明度、分页
状态和禁用项样式。可用区域小于 `452x366` 时自动关闭。`IN_KeyEvent` 在菜单可见时消费数字导航键
的按下与释放，防止底层游戏绑定同时触发。

`L4N::Env` 当前读取：

- `l4n_game_hud_visible`
- `l4n_patch_hud_scope`
- `l4n_vm_sway`
- `l4n_vm_sway_ignore_helpinghand`

其中只有 HUD 可见性直接参与每帧准星门控；其他值主要用于一次性冲突诊断日志。

### 7.1 击杀反馈

`KillFeedback` 只处理本地玩家产生的 `infected_death`、`player_death` 和 `witch_killed`。普通感染者
与特感分别受总开关控制，Smoker、Boomer、Hunter、Spitter、Jockey、Charger、Tank 和 Witch 另有
独立开关。玩家型特感优先从死亡实体的 `m_zombieClass` 识别，实体不可用时回退 `victimname`。
`infected_hurt` 仅用于保存 Witch 最近一次本地伤害的类型，不直接触发反馈。

击杀先分类为普通枪械、爆头、近战或爆炸；启用连杀时，3 秒窗口内第二杀起改用 `2kill..6kill`
动画，声音可递进到 `multikill_10.mp3`。每种效果对应一个 85 帧 VTF；`EngineVGui::Paint` 首次通过
`IMatSystemSurface::DrawSetTextureFile()` 绑定效果纹理，后续使用 `DrawSetTextureFrame()` 切帧并通过
`DrawTexturedRect()` 绘制。不调用 `IMaterialSystem`、不修改全局 `r_screenoverlay`，也不依赖
SourceMod。动画使用 `GlobalVars->realtime`，连杀窗口使用 `curtime`。

## 八、配置与日志

| 文件/变量 | 当前实际用途 |
|---|---|
| `kpatch.ini` | 相对工作目录读取；先读取 `[AdsSupport] enableAdsSupport`，仓库模板没有该段 |
| `[System] debug` | 会被解析，但不控制当前调试开关 |
| `necola\FeatureConfig.json` | 相对工作目录读取/写入，保存 ADS、SequenceModify、Menu 和 KillFeedback 配置 |
| `NECOLA_ADS_DEBUG` | 只要环境变量存在，就启用控制台和 spdlog 详细输出 |
| `L4N-Necola-ADS-diag.log` | 位于宿主 exe 目录；关键里程碑始终写入 |

JSON 保存使用同目录临时文件和 `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)` 替换，避免直接截断正式
文件；解析失败时后续菜单保存会拒绝覆盖。路径仍依赖进程工作目录，而不是 DLL 目录。
若 JSON 包含 `AdsSupport`，后执行的 `AdsMgr.LoadConfig()` 会覆盖 INI 读入的 ADS 开关，因此已有
JSON 配置时 INI 通常不是最终权威值。

## 九、退出与卸载

`DLL_PROCESS_DETACH` 当前直接调用 `ModuleEntry.undo()`：

- 恢复被隐藏的准星。
- 禁用全部 MinHook 并反初始化 MinHook。
- 恢复原窗口过程。

它会恢复 3 个 RecvProp proxy，但没有注销/释放控制台命令，也没有等待可能仍在运行的初始化线程。
此外这些清理发生在 Windows loader lock 下。因此当前只适合进程退出，不支持运行时安全
`FreeLibrary` 或热重载。

## 九、主要维护边界

1. 对所有必需模块、接口、pattern 和 netvar 做统一启动验证，失败时停止并回滚。
2. 把 Hook 安装改为检查返回值的事务流程。
3. 为初始化线程增加取消、句柄保留和 join。
4. 增加显式 shutdown，并恢复 RecvProp、命令和 ADS/bodygroup 状态。
5. 把配置路径固定到明确目录，使用临时文件加原子替换保存。
6. 清理误导性的渲染注释，拆出可测试的 ADS 状态转换和 activity 映射。
