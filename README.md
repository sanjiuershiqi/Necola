<div align="center">
   <img width="360" src="LOGO.png" alt="logo"/></br>

----
an open-source ADS-only plugin for Left 4 Neko (L4N)
</div>

## Necola-ADS (L4N 插件版)

本仓库已改造为 **L4N (Left 4 Neko) 附属插件**，产品功能聚焦 ADS(开镜)，并保留 ADS 依赖的序列修正、bodygroup 修复、菜单和输入模块。

> **开发者文档**：接手本项目前请先阅读 [PROJECT_HANDBOOK.md](PROJECT_HANDBOOK.md)，然后按顺序查阅 [docs/L4N_PLUGIN_RESEARCH.md](docs/L4N_PLUGIN_RESEARCH.md)、[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)、[docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。

### 工作原理

- 不再使用自带 Detours 注入器(`left4dead2_necola.exe` 已删除)。
- 按 L4N v2.43.0 官方 SDK 示例,编译产物 `necola_ads.dll` 放入 `<left4dead2>/bin/neko/plugins/`。
- DLL 导出 `GetL4NPluginInstance`,返回 `IL4NPlugin*` 实例(见 [l4n_plugin.h](necola/l4n_plugin.h));L4N 内部具体加载 API 未公开。
- `OnGameLaunch` 或任意 `OnModuleLoaded` 回调中先到者通过原子门控启动初始化线程;线程每秒检查 8 个 Source 模块。线程创建失败允许后续回调重试,模块等待超时或必需接口/pattern/Hook 缺失时会记录原因并保持插件停用。
- ADS hook 主要使用 Source 接口和 pattern scan,同时读取少量 `l4n_*` cvar 协调 HUD/sway;升级后仍需验证接口和 pattern。

### 目录结构

```
necola/
├── dllmain.cpp        # L4N 插件入口:导出 GetL4NPluginInstance
├── l4n_plugin.h       # L4N 官方插件 SDK (IL4NPlugin 接口)
├── vars.h             # 全局配置 (L4N 主题版本号)
├── hook/
│   ├── Entry.cpp      # 接口获取 + MinHook 安装 + ADS 子系统初始化
│   ├── Hooks.cpp      # 5 个 Raw hook 组(当前 11 个 MinHook detour)
│   ├── Vars.h         # ADS 相关变量
│   └── Feature/       # AdsSupport / SequenceModify / BodygroupFix / MenuManager / ...
└── sdk/               # Source SDK 接口与工具
```

### 构建环境

- Windows + MSVC (x86)
- [xmake](https://xmake.io)
- [vcpkg](https://github.com/microsoft/vcpkg):`inipp`(minhook 与 spdlog 由 xmake-repo 自动拉取)
- 依赖已在 [xmake.lua](xmake.lua) 中声明,首次 `xmake` 会自动拉取。

### 构建

#### 方式一:本地构建

```bash
# release 构建
xmake f -m release -p windows -a x86
xmake

# 或使用 just
just build
```

产物:`build/windows/x86/release/necola_ads.dll`

#### 方式二:GitHub Actions(无需本地环境)

仓库已配置 [.github/workflows/build.yml](.github/workflows/build.yml),推送到 `main`/`master` 分支即自动构建。

- **自动触发**:push 到 `main`/`master`、目标为 `main`/`master` 的 PR、打 `v*` 标签
- **手动触发**:GitHub 仓库 → Actions → Build → Run workflow
- **产物下载**:Actions 运行成功后,在运行详情页底部 Artifacts 下载 `necola_ads-<commit>`
- **Release 发布**:打 `v*` 标签后 CI 会构建并尝试发布;仓库需允许 workflow 写入 Releases

环境:`windows-latest` + xmake + vcpkg(已缓存)。无需在本地配置任何工具链。

```bash
# 触发一次 release 发布
git tag v1.4.0
git push origin v1.4.0
```

### 安装

将 `necola_ads.dll` 复制到 L4N v2.43.0 SDK 示例指定的插件目录:

```
<Steam>/steamapps/common/Left 4 Dead 2/bin/neko/plugins/necola_ads.dll
```

或:

```bash
just install   # 先修改 justfile 的硬编码 TARGET;当前默认值不是官方 SDK 路径
```

### 使用

1. 安装含插件 SDK 的 L4N 版本;本文档核对基线为 v2.43.0,其它版本先确认随附 `l4n_plugin.h` 和插件目录。
2. 将 `necola_ads.dll` 放入 `bin/neko/plugins/`。
3. 关键初始化步骤始终写入游戏根目录的 `L4N-Necola-ADS-diag.log`;如需控制台和详细回调日志,在启动前设置环境变量 `NECOLA_ADS_DEBUG`。`[System] debug` 当前不控制此开关。
4. 正常通过 Steam / `left4dead2.exe` 启动游戏。L4N 会自动加载本插件。
5. 游戏内:
   - 控制台执行 `necola_menu` 打开菜单,在 "ADS功能" 子菜单启用 ADS。
   - 菜单使用数字键 `1`-`7` 选择、`8`/`9` 或左右方向键翻页、`0`/Enter/Esc/Backspace 返回；
     PageUp/PageDown 和数字小键盘同样可用。子菜单会保留上次页码，当前配置以绿色 `[当前]` 标记。
   - "诊断与工具" 可切换 ADS/序列日志或立即退出 ADS 状态；"菜单外观" 可保存左/中/右位置和背景透明度。
   - 绑定按键到 `necola_ads` / `necola_ads_mixed` / `necola_ads_foreceback` / `necola_ads_back` 控制开镜状态。
   - 配套修改版 `cs2hud444` 可显示跨小关累计的战役时间，安装方式见 [战役计时器说明](docs/CAMPAIGN_TIMER.md)。
   - 外部 CF 素材包可由 Necola 驱动枪械、爆头、近战、爆炸和连杀反馈，安装方式见 [击杀反馈说明](docs/KILL_FEEDBACK.md)。

### ADS 控制台命令

| 命令 | 作用 |
|---|---|
| `necola_menu` | 切换菜单显隐 |
| `necola_ads` | 切换 ADS 开镜 |
| `necola_ads_mixed` | 切换 MIXED 开镜状态 |
| `necola_ads_foreceback` | 强制回到普通状态 |
| `necola_ads_back` | 回到上一个 ADS 状态 |
| `necola_timer_status` | 在游戏控制台输出战役和当前小关时间 |
| `necola_timer_reset` | 手动清零并重新开始战役计时 |

### 卸载

删除 `bin/neko/plugins/necola_ads.dll` 即可,L4N 下次启动不再加载。当前版本不支持游戏运行时热卸载。

### License
[The Unlicense](LICENSE)

### Thanks
[L4D2Fix](https://github.com/kurikomoe/L4D2Fix) · [l4d2-internal-base](https://github.com/Lak3/l4d2-internal-base) · Left 4 Neko (L4N 插件 SDK)
