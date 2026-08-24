# Necola 开发与排障指南

> 本文以当前仓库和 L4N v2.43.0 SDK/模板为准。

## 一、构建

### 1.1 环境

| 项 | 要求 |
|---|---|
| 系统 | Windows；目标运行环境由 L4N v2.43.0 readme 标为 Windows 10+ |
| 编译器 | MSVC x86 工具链 |
| 构建工具 | xmake；just 仅提供便捷命令 |
| 语言 | C++20 |
| 目标 | Windows x86 shared library |
| xmake 包 | spdlog、MinHook、vcpkg::inipp |
| 内置库 | `necola/libs/json.hpp`、字符串/hash 工具；另有一份未参与构建的 vendored MinHook |

L4D2 是 32 位进程，插件不能构建为 x64。

### 1.2 本地构建

```powershell
xmake f -m release -p windows -a x86
xmake
```

或：

```powershell
just build
```

预期产物：`build/windows/x86/release/necola_ads.dll`。

`xmake.lua` 使用 `necola/hook/**.cpp` 递归收集 Hook 源码；改回单层 glob 会漏编子目录。

### 1.3 justfile 限制

当前 `justfile`：

- `TARGET` 是作者机器上的硬编码路径。
- Windows recipe 使用 `cp`/`rm`/`mkdir`，需要可提供这些命令的 shell。
- 没有读取 `L4D2_PLUGINS` 环境变量。
- `run` recipe 的旧 preset/target 路径不对应当前 DLL 布局。

使用 `just install` 前必须检查 `TARGET`。官方 SDK 示例路径是
`<L4D2>/bin/neko/plugins/`，不是 `<L4D2>/neko/plugins/`。

### 1.4 CI

`.github/workflows/build.yml` 当前行为：

- push 到 `main`/`master`。
- 针对 `main`/`master` 的 pull request。
- `v*` 标签和手动 `workflow_dispatch`。
- Windows latest，xmake latest，x86 Release。
- 缓存 vcpkg 目录并上传 DLL artifact。
- 标签构建尝试通过 `softprops/action-gh-release` 发布 Release。

当前 workflow 没有 `concurrency`、artifact `retention-days` 或显式 `permissions`。若仓库默认
`GITHUB_TOKEN` 是只读，标签发布可能因缺少 `contents: write` 失败。依赖和 action 也没有固定到
精确提交，构建可复现性有限。

## 二、部署

### 2.1 插件位置

L4N 官方 `l4n_plugin.h` 示例写明：

```text
<L4D2>/bin/neko/plugins/necola_ads.dll
```

仓库旧文档和当前 `justfile` 使用过 `<L4D2>/neko/plugins/`，该路径与 v2.43.0 SDK 示例不一致。
部署时以目标 L4N 版本随附的 SDK 和实际安装目录为准。

删除 DLL 后，下次启动不再加载。当前实现不支持游戏运行时热卸载。

### 2.2 L4N 与启动参数

- `-hide_neko` 会禁用 L4N；依赖 L4N loader 的插件预期也不会进入正常加载路径，但这一点仍应
  在目标版本实测。
- `-insecure` 是官方 readme 给担忧 VAC/服务器安全用户的可选措施，不是已证实的插件加载条件。
- L4N 自带 `left4dead2 no-insecure.bat`，只使用 `-steam -novid`。
- `-vulkan` 不是使用 DXVK 的唯一方式，官方更推荐把 32 位 `d3d9.dll` 放到游戏 exe 旁。
- `-l4n_use_neko_engine_post` 只用于 L4N 后处理，不是 Necola ADS 的前置条件。

不要把一组历史启动参数整体描述为本插件必需参数。

## 三、配置

### 3.1 `kpatch.ini`

文件通过相对路径 `kpatch.ini` 读取，取决于游戏进程工作目录。

- `LoadIni()` 解析 `[System] debug/target/cmdline`。
- `G::Vars.Load()` 先解析 `[AdsSupport] enableAdsSupport`；若后续 JSON 含 `AdsSupport`，JSON 会覆盖该值。
- 当前仓库模板只有 `[System]`，没有 `[AdsSupport]`。
- `[System] debug=true` 不会启用当前调试模式；代码只检查环境变量 `NECOLA_ADS_DEBUG` 是否存在。

### 3.2 `FeatureConfig.json`

代码路径固定为相对路径：

```text
necola\FeatureConfig.json
```

若游戏工作目录是根目录，通常解析为 `<L4D2>/necola/FeatureConfig.json`，不是
`<L4D2>/neko/FeatureConfig.json`。

文件保存 ADS、SequenceModify、菜单外观和 KillFeedback 配置。模式值会限制在合法范围，错误字段
类型回退到默认值。
保存先写 `FeatureConfig.json.tmp`，再通过 `MoveFileEx` 替换正式文件；若现有 JSON 无法解析，菜单
操作不会用空配置覆盖损坏文件。I/O 失败当前仍只静默保留旧文件。

菜单新增持久化字段：

```json
"Menu": {
    "Anchor": 0,
    "BackgroundOpacity": 220
}
```

`Anchor` 为 `0=左侧`、`1=居中`、`2=右侧`，透明度限制在 `160..245`。

### 3.3 按键绑定

Necola 注册：

```text
necola_menu
necola_ads
necola_ads_mixed
necola_ads_foreceback
necola_ads_back
necola_timer_status
necola_timer_reset
```

可直接在控制台使用 `bind`：

```text
bind MOUSE3 necola_ads
```

也可把命令加入 L4N `<L4D2>/left4dead2/neko/config.vdf` 的 `key_bind_acts`。模板只证明配置
语法，插件命令出现在设置菜单中的时序应在目标版本实测。

Necola 不读取 `FeatureConfig.json` 中的 `KeyBinds` 数组，也不会自动执行 `bind`。

菜单打开时会消费数字键及数字小键盘的按下/释放，避免游戏绑定穿透；`0`、Enter、Esc 返回。

### 3.4 `cs2hud444` 战役计时适配

`cs2hud444_timer_patch/materials/vgui/yarou/hud_time/` 保存六个可审查的 VMT 覆盖文件。它们移除
`CurrentTime` proxy，保留 `$frame` 供 `CampaignTimer` 写入。原始第三方 VPK、完整解包目录、下载
工具和重新打包的 VPK 都是本地生成物，不进入仓库。

完整安装与计时规则见 [`CAMPAIGN_TIMER.md`](CAMPAIGN_TIMER.md)。

### 3.5 外部击杀反馈素材

击杀反馈素材不进入 DLL，也不提交到仓库。运行时素材来自外部 addon 包 `skeeto_killfeed.vpk`
（`Necola_analysis/addons/`），安装到 `<L4D2>/left4dead2/addons/` 自动挂载；插件引用其中的
`materials/skeeto/ci|si/...` 图标和 `sound/skeeto/...` 音效。不需要旧 CF 目录或
`addons/sourcemod/plugins/hitsound_v2.smx`。

配置段为：

```json
"KillFeedback": {
    "Enabled": false,
    "CommonEnabled": true,
    "SpecialEnabled": true,
    "SmokerEnabled": true,
    "BoomerEnabled": true,
    "HunterEnabled": true,
    "SpitterEnabled": true,
    "JockeyEnabled": true,
    "ChargerEnabled": true,
    "TankEnabled": true,
    "WitchEnabled": true,
    "IconEnabled": true,
    "SoundEnabled": true,
    "FirearmEnabled": true,
    "HeadshotEnabled": true,
    "MeleeEnabled": true,
    "ExplosionEnabled": true,
    "MultiKillEnabled": true,
    "HitMode": 1,
    "SiDedicated": true,
    "SiSound": true,
    "SoundVolume": 100,
    "SiTheme": "si_cf",
    "CiTheme": "ci_cf"
}
```

完整安装和分类规则见 [`KILL_FEEDBACK.md`](KILL_FEEDBACK.md)。

## 四、日志与调试

### 4.1 始终存在的诊断日志

`RawLog`/`ELog` 把关键启动步骤写到宿主 exe 目录：

```text
<L4D2>/L4N-Necola-ADS-diag.log
```

该日志不依赖 `[System] debug`，会记录模块等待、接口指针、偏移和初始化步骤。

### 4.2 详细调试

在启动游戏前设置环境变量 `NECOLA_ADS_DEBUG`，只要变量存在就会：

- 创建控制台。
- 初始化 spdlog 到同一个诊断日志。
- 使用 trace 级别并对 trace 立即 flush。
- 记录更多回调、模块和命令行信息。

动画级日志还受 `AdsLog` 和 `SequenceLog` 配置控制。高频 Hook 内同步日志可能造成卡顿，只在
定位问题时短期开启。

## 五、排障

### 5.1 DLL 没有任何日志

检查：

1. DLL 是否位于当前 L4N SDK 约定的 `bin/neko/plugins`。
2. 是否使用 `-hide_neko` 禁用了 L4N。
3. DLL 是否为 x86，依赖 DLL 是否可解析。
4. `GetL4NPluginInstance` 是否以 C 符号正确导出。

不要把缺少 `-insecure` 当作首要原因。

### 5.2 初始化停在模块等待

线程每 10 秒记录一次缺失模块，最多等待约 120 秒。超时会记录
`module wait timed out; initialization aborted` 并保持插件停用。

特别检查：

- 等待列表中的 8 个 DLL（包括 `vstdlib.dll`）是否已加载。
- 是否有其他补丁改变模块名称或加载时序。

### 5.3 接口或 pattern 失败

接口日志会输出 17 个指针。当前会集中验证活动功能必需的接口与 pattern；失败项会以
`ERROR: missing required interface/offset` 记录并终止初始化。`ICvar` 是可降级项。

Pattern 为 0 只表示目标二进制与签名不匹配。适配步骤：

1. 确认 L4D2 和 L4N 版本。
2. 获取实际加载的目标 DLL。
3. 用 IDA/Ghidra 验证签名唯一性和指针解引用层级。
4. 更新集中必需 offset 列表，并确认相关 Hook 返回成功。

### 5.4 指令存在但 ADS 无响应

检查：

- `enableAdsSupport` 是否开启。
- 当前是否已连接并进入游戏，玩家是否存活。
- 玩家和武器是否允许主攻击。
- 当前 viewmodel 是否包含 Necola 识别的 ADS/MIXED activity/sequence。
- 原生 scope 武器的 per-weapon scope mode 是否禁用了 ADS。
- 0.4 秒状态切换锁是否仍在生效。

### 5.5 准星或 scope 异常

按顺序对比：

1. Necola ADS 关闭。
2. `l4n_patch_hud_scope 0/1`。
3. `l4n_game_hud_visible 0/1`。
4. 移除武器 MOD 的 `materials/vgui/l4n/scope_*` 资产。
5. 手动检查 `crosshair` 原值是否被正确恢复。

l4nscope 与 Necola 一定冲突并非已证实结论，需要按武器和 MOD 做组合测试。

### 5.6 退出或热卸载崩溃

当前只应随游戏进程退出，不应运行时 `FreeLibrary`：

- 初始化线程无法取消或 join。
- RecvProp proxy 会恢复，但初始化线程仍可能与清理并发。
- 控制台命令不会注销。
- detach 清理运行在 loader lock 下。

## 六、开发检查清单

修改核心逻辑后至少验证：

1. x86 Release 构建和 CI artifact。
2. 干净 L4N 环境启动，18 个接口和必需 pattern 全部有效。
3. 普通武器、原生 scope 武器、双持手枪和无 ADS 动画武器。
4. ADS 1-4 层跳级、MIXED 组合、forceback 和 back。
5. 换枪、死亡、切图、失去连接后的状态复位。
6. `l4n_patch_hud_scope`、HUD 可见性、sway 和 viewmodel offset 组合。
7. 菜单、准星用户值恢复和配置写回。
8. 战役计时跨关累计、任务失败重开、手动重置和配套 HUD 六位数字。
9. 普通感染者、各类特感、Witch 和四种击杀方式的反馈过滤与连杀重置。

仓库当前没有自动化测试、静态分析或警告即错误策略。适合首先抽离为纯逻辑测试的部分是 ADS
状态转换、activity 映射、配置往返和 pattern 解析。

## 七、发布

发布前：

1. 更新 `necola/vars.h` 的插件版本字符串。
2. 确认 HEAD 的 CI 成功，避免只依赖旧 Release DLL。
3. 为 tag workflow 配置可写 Release 权限或确认仓库默认 token 权限。
4. 给发布物记录 L4D2、L4N 和测试过的关键 MOD/配置版本。
5. 说明 DLL 的官方 SDK 部署路径及 SHA-256。

标签命令：

```powershell
git tag v1.4.1
git push origin v1.4.1
```

workflow 会尝试构建并发布，但发布成功与否取决于仓库权限配置。
