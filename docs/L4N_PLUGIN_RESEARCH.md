# L4N 平台逆向研究（基于 v2.43.0 完整重分析）

> 本文档基于 `L4N_v2.43.0.7z` 发行包的解压与逆向分析重写，修正了基于旧版（v2.41）分析的多处结论。
> 分析对象：发行包全部 86 个文件 + 嵌套包 `other_tools.7z` 内的可执行文件。

## 一、L4N 是什么（修正版结论）

**L4N = 修改版游戏主程序 + 引擎 shader 扩展 + 工具链**，不是注入式外挂框架。

| 组件 | 载体 | 作用 |
|---|---|---|
| L4N 主平台 | **修改版 `left4dead2.exe`**（完整安装包内，本增量包不含） | 平台本体：本地化、字体、动画扩充、插件系统等全部功能 |
| Shader 扩展 | `left4dead2/bin/game_shader_generic_neko.dll`（335KB PE32） | 引擎自动扫描 `game_shader_generic_*.dll` 加载，提供 Neko 系 shader（PBR/Toon/Bloom 等） |
| 工具链 | `bin/neko/`（bat 脚本 + survivor_converter + other_tools.7z） | 模型转换、VPK 生成、启动器等开发工具 |

**卸载方式证明主程序被替换**（readme_l4n.txt:78 原文）：
> 在游戏目录里搜索 left4dead2.exe 和 game_shader_generic_neko.dll 删除即可，steam 在启动游戏时会重新下载 left4dead2.exe

**修正**：旧版文档推测"L4N 是注入 DLL/ASI"——错误。L4N 直接替换游戏主程序，随游戏进程启动，无注入过程。这也解释了为什么 `-hide_neko` 启动参数就能禁用它（exe 内部判断参数后走原版逻辑）。

**插件系统引入时间**：更新日志第 653 行（2.34.0 前后）"新增：l4nplugin插件系统"。

## 二、发行包结构（v2.43.0 实测，86 文件 / 19.5MB）

```
L4N_v2.43.0.7z
├── readme_l4n.txt                  # 1601 行说明 + 完整更新日志
├── left4dead2 no-insecure.bat      # 免 -insecure 启动脚本
├── bin/neko/                       # 开发者工具
│   ├── plugins/l4n_plugin.h        # ★ 插件 SDK 头文件（含官方示例）
│   ├── other_tools.7z              # ★ 嵌套包:nekook 启动器/nekomdl 模型工具/VPK shell 扩展
│   ├── survivor_converter/         # 8 个幸存者基准 .mdl
│   ├── to_l4n_survivor.bat         # 幸存者模型转换流水线
│   ├── survivor_vpk_gen.bat        # VPK 打包
│   ├── build_sound_cache.bat
│   └── copy_sentence.bat
├── left4dead2/
│   ├── bin/
│   │   ├── game_shader_generic_neko        # 28 字节文本标记(内容即 dll 文件名)
│   │   └── game_shader_generic_neko.dll    # ★ 引擎自动加载的 shader DLL
│   ├── materials/l4n/              # bloom_map/brdf_lut/neko_tonemap 等内嵌资源
│   ├── neko/                       # ★ 配置模板目录(见第六节)
│   │   ├── config_template.vdf
│   │   ├── localize_overrides_template.vdf
│   │   ├── scheme_overrides_template.vdf
│   │   ├── sequence_event_template.vdf
│   │   ├── server_name_filter_template.txt
│   │   ├── l4ngui_schinese.vdf / l4ngui_english.vdf   # GUI 本地化(UTF-16LE VDF)
│   │   ├── mdl_extension.qc        # ★ 模型扩充 QC 模板(动画扩充用)
│   │   └── neko_proxy.vmt
│   └── shaders/fxc/                # 30+ 编译好的 shader(.vcs)+.vmt
└── reshade-shaders/Shaders/L4N/L4N_Util.fx   # ReShade Bridge 配套
```

注：本包为增量更新包，**不含修改版 left4dead2.exe**（完整包才有，readme:76 "仅安装着色器则跳过复制exe文件"可证）。

## 三、插件接口契约（v2.43.0 原文）

### 3.1 接口定义
`bin/neko/plugins/l4n_plugin.h` 全文核心（与旧版接口完全一致，未破坏兼容）：

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
typedef IL4NPlugin* (*GetL4NPluginInstanceFunc)();
```

### 3.2 官方示例（v2.43.0 新增，旧版没有）
头文件尾部注释块给出了**官方钦定写法**：

```cpp
class MyPlugin : public IL4NPlugin {
public:
    void OnModuleLoaded(const char* module_name, std::uintptr_t handle) override {
        std::string_view name = module_name;
        auto hModule = std::bit_cast<HMODULE>(handle);   // ← handle 就是 HMODULE
        if (name == "client") { /* do some thing... */ }
        else if (name == "engine") { /* do some thing... */ }
    }
};
extern "C" __declspec(dllexport) IL4NPlugin* GetL4NPluginInstance() {
    static MyPlugin instance;    // ← static 局部实例,官方模式
    return &instance;
}
```

**要点**：
- `handle` 参数 = `LoadLibrary` 返回的 `HMODULE`，可直接用于 `GetProcAddress`——官方明确此用途
- 模块名匹配用 `name == "client"`（**无 .dll 后缀**），与我们日志实测一致
- 官方示例即我们的实现方式（导出 C 函数 + static 实例）

### 3.3 插件加载流程（推断 + 实测验证）
主平台 exe 内的加载逻辑无法直接反汇编（exe 不在包内），但由接口设计 + 行为日志可确认：

1. 游戏启动早期（引擎模块加载阶段），L4N 扫描 `<left4dead2>/neko/plugins/*.dll`
2. `LoadLibraryExA` 加载 → 触发插件 `DllMain(DLL_PROCESS_ATTACH)`
3. `GetProcAddress(hModule, "GetL4NPluginInstance")` → 调用得 `IL4NPlugin*`
4. 引擎每加载一个 DLL，L4N 回调 `OnModuleLoaded(name, hModule)`
5. 游戏启动完成前回调 `OnGameLaunch`

**回调时机实测**（来自我们插件的游戏日志）：
```
OnGameLaunch()                     ← 此时 client.dll 等均未加载!
OnModuleLoaded("engine", ...)      ← 紧接着逐个上报
OnModuleLoaded("inputsystem"/"materialsystem"/"datacache"/...)
OnModuleLoaded("client", ...)      ← 关键节点
OnModuleLoaded("server"/"gameui")
OnModuleLoaded("vaudio_miles")     ← 较晚
OnModuleLoaded("serverbrowser")    ← 最晚之一
```

**关键限制**（实测确认）：L4N **不上报所有模块**——`filesystem_stdio.dll`、`vstdlib.dll` 从不回调。因此插件**不能纯依赖回调**判断就绪，必须保留 `GetModuleHandleA` 兜底轮询（我们的实现正是如此）。

### 3.4 D3D 回调（未验证）
`OnD3DCreated`/`OnD3DDeviceCreated(void*, bool is_dxvk)` 设计用于渲染相关插件。`is_dxvk` 参数说明 L4N 官方考虑了 dxvk 场景（用户用 `-vulkan` 时 dxvk 生效）。本项目未使用（我们的菜单走 VGUI Paint 而非 D3D 绘制）。

## 四、necook 与其他工具（other_tools.7z 分析）

### 4.1 nekook（第三方启动器，非 L4N 官方）
- `nekook.exe`（463KB PE32 console）+ `nekook_core.dll`（7.4MB PE32 DLL）
- 版本 "Nekook v1.6 - by Starfelll"，更新日志注明"1.6 支持l4n"
- 用途（nekook_readme.txt）：**mod 开发启动器**——把任意目录挂载进 Source 文件系统（优先级高于 vpk）、免打包测试、配合 nekomdl 热重载 mdl/vmt
- 关键字符串证据：`INJECT ERROR: %s`、`Nekook_Main` 导出、`-nekook "`/`-vpkwhitelist`/`left4dead2.exe`/`-hide_neko`、`too many neko for index buffer`
- `nekook_core.dll` 导出表（70 个，全部 `LM_*` 前缀）= **LibAsmMagic 运行时库**：`LM_PatternScan(Ex)`、`LM_HookCode(Ex)`、`LM_Assemble/Disassemble`、`LM_Alloc/FreeMemory`、`LM_EnumModules/Processes/Symbols`、`LM_FindSymbolAddress` 等——与我们插件的 MinHook + 自研 pattern scan 属同类技术栈；nekook 用它给引擎打 index buffer 扩容补丁（对应 config 的 `index_buffer_size`）
- 游戏内亦有配套命令：`l4n_nekook_path_append/remove`（readme:471-472，运行时挂载搜索路径）
- **结论**：nekook 是开发期工具。玩家正常运行游戏不需要它；插件作者可用它加速"改资源→进游戏"迭代

### 4.2 nekomdl（模型工具）
`nekomdl.exe`（7.2MB PE32+ x64）+ `nekomdl.7z`（源码/文档）。配合 nekook 实现 **mdl/vmt 热重载**（不重启游戏更新模型）——对做 ADS 动画模型的作者有价值。

### 4.3 VPKShellExtensions
资源管理器集成：vpk 缩略图 + mod 标题元信息显示。

## 五、与 ADS 插件直接相关的 L4N 机制

逆向更新日志与配置模板发现的、与本项目功能强相关的官方机制：

### 5.1 幸存者动画扩充（max_sequences）
config.vdf `survivors` 段为 8 名幸存者设定 `max_sequences`（如 nick=923, zoey=947）。L4N 的动画导入机制：同名动画不导入，否则在末尾扩充。readme 长注释解释了服务端按**序号**同步动画、按 **Activity** 调用动画的机制，以及 `l4n_survivor_sequence_strip` cvar 可截断扩充动画。

**对本项目的意义**：ADS 动画走 weapon viewmodel（非幸存者模型），但同样的"序号同步 + Activity 调用"原理约束着我们的序列重映射逻辑——服务器不知道客户端扩充的 ADS 序列，所以我们必须在客户端把服务器序列号重映射到本地扩充序列（这正是 SequenceModify 模块存在的根本原因，官方注释佐证了设计正确性）。

### 5.2 sequence_event.vdf（动画事件粒子重定向）
```
// 动画事件设置，目前只支持第一人称武器v模
"sequence_event" {
    "particle_map" {
        // "old_name" "new_name"   重定向粒子
        // "name" ""               留空则忽略该粒子
    }
}
```
**第一人称武器 v 模动画事件**的官方拦截点。若 ADS 动画的枪口焰/粒子事件需要调整，可建议用户用此官方机制，而不必改插件。

### 5.3 l4n_vm_sway_ignore_helpinghand
readme:383 原文："是否在触发伸手动画时禁用sway效果，**启用后可能会导致一些插件的动画(如ADS)没有sway效果**"。
**L4N 官方更新日志直接把 ADS 列为插件生态案例**——证明本项目的 ADS 插件定位与官方预期完全吻合，且 L4N 的 sway（武器摆动）系统会与插件 ADS 动画交互。排查"ADS 时武器不摆动/抖动"问题时应先检查此 cvar。

### 5.4 字体替换（font.replace）
config.vdf 可做全局字体替换（如 Tahoma→微软雅黑）。我们菜单用 `Microsoft YaHei` 25px——若用户系统缺该字体，可指引其通过 L4N font 配置全局兜底。

### 5.5 l4nscope：L4N 原生自定义开镜（重大发现，与本项目同域！）

v2.43.0 readme:357 将 `l4nscope` 列为"以下功能需要MOD适配"之一；更新日志 [2.28.0] - 2026-05-24："新增：**以v模为单位的武器自定义开镜功能**"。

即 L4N 平台原生支持**武器 viewmodel mod 内置开镜效果**——与我们的 ADS 插件属同一功能域。

配套的 HUD 侧机制：
- `l4n_patch_hud_scope 1`（readme:428）：是否**接管狙击枪开镜 HUD 的渲染**
- `l4n_hud_scope_draw_padding_block`（readme:427）：HUD 开镜黑边填充
- GUI 项 `🐱allow_hud_hud_scope_display`（l4ngui_schinese.vdf）："允许HUD显示开镜"

**对本项目的意义**：
1. **共存问题**：若用户武器 mod 已适配 l4nscope，同时开启插件 ADS 可能双重处理开镜状态。建议后续在插件里检测/文档说明二者选一，或提供开关
2. **HUD 依赖**：`l4n_patch_hud_scope` 接管狙击枪开镜 HUD 时，我们的准星隐藏逻辑（Paint 钩子里 `crosshair 0/1`）可能与 L4N 的接管渲染交互——排查"开镜时准星异常"要先确认此 cvar 状态
3. **定位差异**：l4nscope 需要每个武器 mod 单独适配（QC 层），我们的 ADS 是通用序列重映射（运行时层，零适配）——互补关系而非纯竞争

## 五A、L4N 完整功能面（l4ngui_schinese.vdf 逆向）

GUI 本地化文件（116 行，约 100 个 token）完整暴露 L4N 游戏内设置项。按类归纳：

| 分类 | 功能项（节选） |
|---|---|
| **屏幕后处理**（需 `-l4n_use_neko_engine_post`） | 色调映射（Neutral/Unity 推荐、Linear/Reinhard/Filmic/CE 等曲线）、亮度、伽马、场景曝光、局部对比度（仅负/仅正/全启用）、暗度限制（lightmap/ambient 分开）、NekoBloom 泛光（强度/激发亮度/亮度限制/半径/混合模式/遮罩贴图）、NekoSky 亮度与叠加纹理 |
| **第一人称设置** | 伸手动画（物品/援助）、sway 滞后时间、禁止手指拉伸、模型位置偏移、自发光 ±10%/重置、VLG↔PBR 材质互转、手模转 NekoToon |
| **实体相关** | 随机身高、玩家实体染色（禁用/重置/随机）、幸存者光照强度、全局幸存者缩放、准星处角色模型转 NekoToon |
| **开镜/准星**（与 ADS 同域） | 允许 HUD 显示开镜、HUD 开镜空白区域填充、精确第三人称准星（缩放/动态/透明度）、`l4nscope` 自定义开镜 |
| **手电筒** | 亮度倍率、R/G/B 颜色倍率、手电筒枪火、动态枪火光源（遵循/忽略消音粒子、最大距离、抑制曝光） |
| **贴花** | 静态道具/实体/特感/幸存者/普感 5 类独立开关 |
| **HUD/界面** | 队友状态显示、接管 HUD 队友信息（实验）、玩家列表（Steam 头像/胆汁图标裁剪）、内存占用显示、Hud 菜单位置 |
| **NekoToon** | 描边（禁用/世界空间厚度/屏幕空间厚度）、描边厚度倍率、统一 LightWarp |
| **杂项** | 快速录 DEMO、禁普感布娃娃、屏幕震动强度、受击晃动强度、实体消逝、一致性检查、第三人称开火声音修复、自定义指令菜单 |
| **实验性** | 接管 HUD 队友信息、接管 ReShade 效果渲染（`l4n_reshade_draw`，HUD 前渲染 ReShade 滤镜） |

**关键 cvar 速查**（readme 完整表 60+ 个，最相关节选）：

| cvar | 作用 | 与本项目关系 |
|---|---|---|
| `l4n_vm_sway` / `l4n_vm_sway_interp` / `l4n_vm_sway_scale` | v 模摆动开关/恢复时间/幅度 | ADS 动画观感直接相关 |
| `l4n_vm_sway_ignore_helpinghand` | 伸手动画时禁 sway | 官方注释点名影响"插件的动画(如ADS)" |
| `l4n_vm_offset_x/y/z` | 全局 v 模位置偏移 | 与 ADS 武器偏移叠加 |
| `l4n_pin_viewmodel` | 固定 viewmodel 实体 | ADS 视角调试用 |
| `l4n_patch_hud_scope` / `l4n_hud_scope_draw_padding_block` | 接管狙击枪开镜 HUD / 黑边填充 | 开镜 HUD 冲突排查 |
| `l4n_view_punch_scale` / `l4n_screen_shake_scale` | 受击晃动/屏幕震动 | ADS 稳定射击体验 |
| `l4n_game_hud_visible` | HUD 总开关 | 准星隐藏逻辑交互 |
| `l4n_allow_lobby_cheats` | 大厅作弊限制提示 | 部署环境理解 |
| `l4n_scripted_hud_allow[_slot1~15]` | 脚本化 HUD 渲染开关 | 15 个 slot——插件 HUD 扩展点候选 |

**关键 concmd**：
- `l4n_vm_offset2 [x/dx/y/dy/z/dz/reset] [value]`：当前 v 模位置偏移（含增量模式）——ADS 武器偏移官方调法
- `l4n_print_launch_options`：输出当前可识别启动项——部署排障第一命令
- `l4n_env_report`：输出运行环境信息
- `l4n_nekook_path_append/remove "path"`：**运行时**添加/移除高优先级资产搜索路径
- `l4n_reload_config` / `l4n_reload_sequence_event_vdf` / `l4n_reload_vgui_schemes`：三类配置热重载
- `l4n_revert_cvar <name>`：cvar 恢复默认（cvar 值不持久化，建议写 autoexec.cfg）

## 五B、MOD 适配协议（mdl_extension.qc 逆向）

`neko/mdl_extension.qc` 是模型 mod 适配 L4N 的 QC 命令示例，揭示了完整的模型层扩展协议：

1. **`$NekoModel` QC 块**：新 QC 命令，支持 gltf/glb/fbx 源文件直接引用；内含形态键眼追协议——4 个方向 flex + 2 个 flexcontroller（`l4neyes_updown`/`l4neyes_rightleft`），绑定公式 `"%eye_look_up" = max(-l4neyes_updown, 0)`。注释强调：新 flexcontroller 必须加到已有列表**底部**，否则破坏排序导致表情联机同步问题
2. **自定义形态键 UI**：`flexcontroller l4n range 0 1 "名称"`——分组名 `l4n` 的控制器自动进 L4N 模型选项面板
3. **BodyGroup 扩展**：超过 2 个选项需 `⭐` 前缀（防与静态表情冲突）；Model/Body/NekoModel 也计入 BodyGroup，**所有组合不能超过 2147483647**
4. **`$TextureGroup` 材质预设**：多套皮肤切换（需 nekomdl 编译器支持）
5. **`$KeyValues left4neko` 元数据块**：`name`（用户友好名）、`name_suffix`、`player_foot`（**按 bodygroup 选项的足 IK 修正**，如高跟鞋 `"1" "-2.1,-60.0"`）、`scale`

**其他 Modder 扩展点**（readme For Modder 章节）：
- `neko_proxy.vmt`：平台扩充材质代理文档，现有"获取弹药数量/备用弹药数量"代理
- `l4n/scripts/sound/*.txt`：自动加载为声音脚本（vpk 内需带 sound.cache）
- `l4n/particles/*.pcf`：自动加载粒子（`!` 前缀文件名有对应效果），无需 manifest 依赖

## 六、L4N 全部配置扩展点（v2.43.0 config_template.vdf 全量）

`neko/config_template.vdf` → 复制为 `config.vdf` 生效。全部段落：

| 段/键 | 类型 | 用途 | 插件可复用性 |
|---|---|---|---|
| `environment_variables` | 段 | 设 VK layer 黑/白名单（`VK_LOADER_LAYERS_DISABLE`/`ENABLE`），解决 dxvk+ReShade 内存异常 | 环境，间接 |
| `custom_commands` | 段 | 自定义指令挂入 `l4n_custom_command_menu` 菜单。支持 `cmd`（命令串）/`cvar`（可 `delta`/`min`/`max` 步进切换） | **高**：可把 `necola_ads` 等注册进去 |
| `key_bind_acts` | 段 | 扩展"选项→键盘/鼠标→按键设置"列表，值为命令串 | **高**：`necola_*` 命令可直接加入 |
| `disable_chromehtml` | 键 | 禁引擎内置浏览器，省 ~100MB 内存 | 低 |
| `index_buffer_size` / `vertex_buffer_size` | 键 | 引擎 buffer 扩容（防 "Too many indices" 弹窗），默认 32768 | 环境（nekook 补丁配套） |
| `font.anti_aliasing` / `font.min_tall` / `font.replace` | 段 | 全局字体抗锯齿/最小字号/字体名替换 | 中（菜单字体兜底） |
| `launch_options` | 段 | L4N 代加启动项。`"-novid" ""` / `"+cvar" "value"` 写法。**注意 `-hide_neko` 在此无效**，必须真实启动参数 | **高**：部署文档可指引 |
| `force_bind_l4n_menu` | 键 | 强制绑 `l4n_menu` 到 `\` 键 | 低 |
| `lock_datacache_size` | 键 | datacache 锁 2048MB | 环境 |
| `mimalloc` | 键 | 实验性：mimalloc 替换 tier0 分配器 | 环境（实验性，dxvk 下易崩） |
| `survivors.<role>.max_sequences` | 段 | 每角色动画序列上限（见 5.1） | 机制参考 |

### 6.1 key_bind_acts 机制结论（v2.43.0 再确认）
模板实配条目（`l4n_menu`、`+l4n_lookat`、`thirdpersonshoulder` 等）再次确认：
- **不校验命令来源/注册状态**——含引擎原生命令 `thirdpersonshoulder`
- 条目 = 显示名 + 命令串，进游戏按键设置 UI，绑定的键由引擎 `bind` 机制触发
- 本插件用法（已验证可行）：
  ```
  "key_bind_acts" {
      "Necola ADS 开镜"      "necola_ads"
      "Necola ADS 混合状态"  "necola_ads_mixed"
      "Necola ADS 强制复位"  "necola_ads_foreceback"
      "Necola ADS 上一状态"  "necola_ads_back"
      "Necola 菜单"          "necola_menu"
  }
  ```

### 6.2 其余 4 个配置模板

| 模板 | 另存为 | 用途 |
|---|---|---|
| `localize_overrides_template.vdf` | `localize_overrides.vdf` | 本地化文本键值覆盖（角色名等） |
| `scheme_overrides_template.vdf` | `scheme_overrides.vdf` | VGUI 主题覆盖（字体样式等），控制台 `l4n_reload_vgui_schemes` 热重载 |
| `sequence_event_template.vdf` | `sequence_event.vdf` | 第一人称武器动画粒子重定向（见 5.2） |
| `server_name_filter_template.txt` | `server_name_filter.txt` | 服务器名黑名单（正则，过滤 RPG 服等） |

## 七、我们的模块等待实现（沿用，依据补强）

问题：`OnGameLaunch` 时引擎模块全未加载，但 `CreateInterface` 需要 client.dll/engine.dll 等已加载。

实现（[necola/dllmain.cpp](../necola/dllmain.cpp)）：
1. **事件驱动**：每次 `OnModuleLoaded` 回调 `notify_all` 唤醒等待线程（官方 handle=HMODULE 用法已按 3.2 节确认，但接口获取走 `CreateInterface` 更直接，未存 handle）
2. **兜底轮询**：100ms `GetModuleHandleA` 检查 7 个关键模块（`filesystem_stdio` 等 L4N 不上报的模块靠它）
3. **单线程保证**：`std::call_once` 防止 OnGameLaunch/OnModuleLoaded 并发各起线程（历史教训：曾因此 MinHook 重复初始化崩溃）

等待清单：`client.dll, engine.dll, vgui2.dll, datacache.dll, vguimatsurface.dll, inputsystem.dll, filesystem_stdio.dll`

## 八、特征码失效案例（L4N 修改引擎的实证）

[Offsets.cpp](../necola/sdk/Offsets.cpp) 的 `CParticleSystemMgr` pattern（`0C 8B 0D ? ? ? ? 52 50 E8 82 5F`）在 L4N 修改过的 client.dll 上扫描失败返回 0。崩溃日志实证：
```
Step 3.5: Offsets check: m_dwCParticleSystemMgr=00000000
Step 3.6: dereferencing offsets ...
!!! SEH exception 0xC0000005 in ModuleEntry::Load
```
应对：所有 pattern 偏移解引用前判空（Entry.cpp Step 3.6）。ADS 功能不依赖 ParticleSystemMgr，null 时安全跳过。

**注意**：L4N 既然替换了游戏主程序，引擎 DLL 是否被改以版本而异；每次 L4N 大版本更新后应跑一遍日志确认 pattern 命中情况。

## 九、仍未逆向的部分（诚实记录）

1. **修改版 left4dead2.exe 本体**：不在 v2.43.0 增量包内（完整安装包才有）。插件加载循环、config.vdf 解析、按键列表注入、`l4nscope` 的 mod 适配判定等实现细节无法二进制级确认。如需深入：从游戏安装目录取该 exe（Steam 会校验 hash，删除后重新"验证完整性"即得原版可作 diff 基准）。
2. **`l4nscope` 的适配格式**：readme 仅在"需要MOD适配"列表提及，包内未发现 l4nscope 的 QC/KeyValues 格式文档（可能随完整包或需询问作者）。当前只能确认其存在与版本（2.28.0）。
3. **`game_shader_generic_neko.dll` 内部逻辑**：仅 3 个标准导出（CreateInterface/cvar/g_pCVar），是纯 shader DLL，未见插件加载痕迹；未做完整反汇编。
4. **D3D 回调实际行为**：未实测（用户走 -vulkan/dxvk 路径）。
