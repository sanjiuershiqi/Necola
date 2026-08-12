<div align="center">
   <img width="360" src="LOGO.png" alt="logo"/></br>

----
an open-source ADS-only plugin for Left 4 Neko (L4N)
</div>

## Necola-ADS (L4N 插件版)

本仓库已改造为 **L4N (Left 4 Neko) 附属插件**。只保留 ADS(开镜)功能,其它特性已全部移除。

### 工作原理

- 不再使用自带 Detours 注入器(`left4dead2_necola.exe` 已删除)。
- 编译产物 `necola_ads.dll` 放入 `<left4dead2>/neko/plugins/`。
- L4N 启动时扫描该目录,`LoadLibraryExA` 加载本 DLL,通过 `GetProcAddress("GetL4NPluginInstance")` 取得 `IL4NPlugin*` 实例(见 [l4n_plugin.h](necola/l4n_plugin.h))。
- L4N 在 `OnModuleLoaded("client")` 回调时触发 Necola 初始化线程;线程等待 `serverbrowser.dll` 加载后,自行用 Source `CreateInterface` 抓取接口并用 MinHook 安装 ADS hook。
- 核心逻辑完全自治,不依赖 L4N 内部 API,升级稳定。

### 目录结构

```
necola/
├── dllmain.cpp        # L4N 插件入口:导出 GetL4NPluginInstance
├── l4n_plugin.h       # L4N 官方插件 SDK (IL4NPlugin 接口)
├── vars.h             # 全局配置 (L4N 主题版本号)
├── hook/
│   ├── Entry.cpp      # 接口获取 + MinHook 安装 + ADS 子系统初始化
│   ├── Hooks.cpp      # 5 个必需 Raw hook
│   ├── Vars.h         # ADS 相关变量
│   └── Feature/       # AdsSupport / SequenceModify / BodygroupFix / MenuManager / ...
└── sdk/               # Source SDK 接口与工具
```

### 构建环境

- Windows + MSVC (x86)
- [xmake](https://xmake.io)
- [vcpkg](https://github.com/microsoft/vcpkg):`minhook`、`inipp`、`spdlog`
- 依赖已在 [xmake.lua](xmake.lua) 中声明,首次 `xmake` 会自动拉取。

### 构建

```bash
# release 构建
xmake f -m release
xmake

# 或使用 just
just build
```

产物:`build/windows/x86/release/necola_ads.dll`

### 安装

将 `necola_ads.dll` 复制到 L4N 的插件目录:

```
<Steam>/steamapps/common/Left 4 Dead 2/neko/plugins/necola_ads.dll
```

或:

```bash
just install   # 默认 TARGET 见 justfile,可按需修改
```

### 使用

1. 确保已安装 L4N v2.41.0+(即 `left4dead2.exe` 为 L4N 启动器、`bin/left4neko.dll` 存在)。
2. 将 `necola_ads.dll` 放入 `neko/plugins/`。
3. (可选)把本仓库根目录 `kpatch.ini` 复制到游戏目录,设置 `[System] debug=true` 可开启控制台与详细日志(日志文件 `L4N-Necola-ADS.log`)。
4. 正常通过 Steam / `left4dead2.exe` 启动游戏。L4N 会自动加载本插件。
5. 游戏内:
   - 控制台执行 `necola_menu` 打开菜单,在 "ADS功能" 子菜单启用 ADS。
   - 绑定按键到 `necola_ads` / `necola_ads_mixed` / `necola_ads_foreceback` / `necola_ads_back` 控制开镜状态。

### ADS 控制台命令

| 命令 | 作用 |
|---|---|
| `necola_menu` | 切换菜单显隐 |
| `necola_ads` | 切换 ADS 开镜 |
| `necola_ads_mixed` | 切换 MIXED 开镜状态 |
| `necola_ads_foreceback` | 强制回到普通状态 |
| `necola_ads_back` | 回到上一个 ADS 状态 |

### 卸载

删除 `neko/plugins/necola_ads.dll` 即可,L4N 下次启动不再加载。

### License
[The Unlicense](LICENSE)

### Thanks
[L4D2Fix](https://github.com/kurikomoe/L4D2Fix) · [l4d2-internal-base](https://github.com/Lak3/l4d2-internal-base) · [Left 4 Neko](https://github.com/) (L4N 插件 SDK)
