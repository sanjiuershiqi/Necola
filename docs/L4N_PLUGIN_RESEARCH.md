# L4N 平台与插件接口研究

> 基线：仓库内 `L4N_extracted` 保存的 L4N v2.43.0 发行材料，以及当前 Necola 源码。
> 本文严格区分发行材料直接证明的事实、Necola 当前实现和仍待运行时验证的推断。

## 一、证据范围

本文使用三种证据等级：

| 等级 | 含义 | 示例 |
|---|---|---|
| 直接证据 | 官方头文件、readme 或模板明确写出 | `IL4NPlugin` 方法、`config.vdf` 结构 |
| 实现事实 | Necola 当前源码中的行为 | 1 秒模块轮询、MinHook 安装 |
| 待验证 | 从接口形态推断，但发行材料没有给出实现 | L4N 内部具体加载 API、回调线程和顺序 |

当前 `L4N_extracted` 可见 71 个文件。它不包含修改后的 `left4dead2.exe`、实际的
`game_shader_generic_neko.dll` 或解开的工具可执行文件，因此不能据此完成加载器或 shader DLL
的二进制级逆向。历史文档中关于 PE 大小、导出表、`LoadLibraryExA` 和 loader 扫描循环的描述，
均不属于当前证据集能够独立复核的事实。

## 二、L4N 的可确认定位

官方 readme 将 L4N 定义为 L4D2 客户端补丁，并声明支持游戏版本 2.2.4.3、Windows 10+：

- 安装方式是把发行包覆盖到游戏根目录。
- 仅安装 shader 时可跳过复制 exe。
- 卸载说明要求删除被替换的游戏 exe 和 `game_shader_generic_neko.dll` 后由 Steam 恢复。
- `-hide_neko` 可禁用 L4N。

这些材料足以说明 L4N 的分发方式包含游戏主程序补丁/替换，而不是把它描述成一个普通的
独立 DLL 插件框架。但修改版 exe 不在仓库中，所以内部如何加载模块、是否使用额外注入技术、
`-hide_neko` 的具体分支实现仍未知。

L4D2 是 32 位进程，readme 也明确提醒不能给游戏使用 64 位 DXVK DLL。因此进程内 L4N 插件
必须构建为 x86；官方 SDK 没有进一步规定编译器版本或 CRT 链接方式。

## 三、插件 ABI

直接证据来自
[`L4N_extracted/bin/neko/plugins/l4n_plugin.h`](../L4N_extracted/bin/neko/plugins/l4n_plugin.h)。

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

接口表面是：一个虚析构函数、3 个元数据方法和 4 个回调。不能把它计为“5 个回调”。

官方示例直接证明以下契约：

- DLL 示例路径为游戏根目录下的 `bin/neko/plugins/MyPlugin.dll`。
- DLL 导出无名称修饰的 C 符号 `GetL4NPluginInstance`。
- 工厂返回一个静态生命周期的 `IL4NPlugin` 实例。
- `OnModuleLoaded` 的整数句柄预期通过 `std::bit_cast<HMODULE>` 使用。
- 示例模块名是 `client`、`engine`，没有 `.dll` 后缀。

ABI 没有规定：

- 返回对象的所有权和宿主是否会调用析构函数。
- 回调调用次数、先后顺序、线程、重入规则和异常处理。
- `OnModuleLoaded` 是否覆盖所有模块，以及所有模块名的规范化规则。
- D3D 指针的具体 COM 类型、所有权、设备重置和线程规则。
- 卸载、热重载或显式 shutdown 协议。

由于接口是 C++ 虚表 ABI，而工厂只对外暴露 C 符号，插件还隐含依赖宿主兼容的 C++ 对象布局。
接口版本默认返回 1，但发行材料没有展示宿主如何校验该值，也不能仅凭一份头文件断言它长期未变。

## 四、加载与生命周期边界

头文件和目录结构强烈暗示 L4N 在游戏进程内加载 `bin/neko/plugins` 下的 DLL，取得工厂并调用
接口回调。具体使用 `LoadLibrary`、`LoadLibraryEx` 还是等价机制，发行材料没有说明。

当前 Necola 不依赖未经证实的回调顺序：

1. `OnGameLaunch` 或任意一次 `OnModuleLoaded` 中先到者通过共享原子标志尝试创建线程。
2. `OnModuleLoaded` 把模块名和句柄缓存到 `L4N::Env`。
3. 初始化线程每 1 秒用 `GetModuleHandleA` 检查 9 个模块，最多循环 120 次。
4. 120 次后仍缺模块会记录超时并中止，不会进入接口获取和 Hook 安装。
5. 接口获取和 pattern scan 仍直接使用 `GetModuleHandleA`，尚未消费 `L4N::Env` 的句柄缓存。

若 `CreateThread` 失败，原子标志会复位，后续 L4N 回调可再次尝试。

等待列表是：

```text
client.dll
engine.dll
vgui2.dll
datacache.dll
vguimatsurface.dll
inputsystem.dll
filesystem_stdio.dll
vstdlib.dll
```

`Entry.cpp` 会从 `vstdlib.dll` 获取 `ICvar`，因此该模块也在等待列表中。当前代码没有条件变量、
回调通知唤醒或 100 ms 轮询，也不等待
`serverbrowser.dll`。

Necola 的两个 D3D 回调为空。菜单和准星逻辑通过 Source VGUI/ConVar 实现，不能据此推导 L4N
D3D 回调在 DX9/DXVK 下的真实语义。

## 五、L4N 配置扩展点

`left4dead2/neko/config_template.vdf` 是 Valve KeyValues 模板，修改后必须另存为 `config.vdf`。
模板明确警告语法错误会使配置失效。

与 Necola 最相关的段：

| 段/键 | 直接可确认用途 | Necola 用法 |
|---|---|---|
| `key_bind_acts` | 给游戏按键设置增加“显示名 -> 命令串”条目 | 可填 `necola_*` 命令；运行时接受情况仍应实测 |
| `custom_commands` | 给 `l4n_custom_command_menu` 增加 `cmd`/`cvar` 条目 | 可把 ADS 命令放进 L4N 自定义命令菜单 |
| `launch_options` | 由 L4N 增加启动参数 | `-hide_neko` 等三项明确不能写在这里 |
| `environment_variables` | 设置 Vulkan layer 环境变量 | 用于 DXVK/ReShade 排障，与 ADS 间接相关 |
| `font.replace` | 全局字体名替换 | 可协调菜单字体，但不能保证缺失字体一定可用 |
| `survivors.*.max_sequences` | 幸存者扩展动画截断阈值 | 只直接适用于幸存者，不证明武器 ADS 的序列协议 |

模板还包含 buffer、datacache、mimalloc、wave cache 和 addoninfo 等环境配置。不要把 GUI 本地化
token 或模板键误称为 L4N 对插件开放的编程 API。

`key_bind_acts` 只证明配置语法。它是否校验命令是否已注册、何时构造按键菜单、是否接受晚注册
插件命令，当前证据集没有运行日志，因此应在目标版本中验证。Necola 自身没有读取
`FeatureConfig.json/KeyBinds` 或自动执行 `bind` 的代码。

## 六、l4nscope 与 ADS

L4N readme 明确列出 `l4nscope`：以武器 viewmodel 为单位、需要 MOD 适配的自定义开镜效果。
`neko_refract.vmt` 给出了实际资产协议：

- 在 `materials/vgui/l4n` 下准备 `scope_xx.vmt`。
- 可选准备左右填充材质 `scope_pl_xx.vmt`、`scope_pr_xx.vmt`。
- `xx` 是武器 viewmodel 文件名，例如 `scope_v_snip_awp.vmt`。
- 主开镜贴图按 4:3 制作，左右缺省区域可由纯黑填充。

因此 l4nscope 当前可确认的是材质/HUD 资产命名协议，不能描述成已证实的 QC 动画协议。

相关 L4N 控制项：

| 控制项 | 官方说明 | 建议验证 |
|---|---|---|
| `l4n_patch_hud_scope` | 接管狙击枪开镜 HUD 渲染 | 与 Necola 准星隐藏和原生 scope 是否叠加 |
| `l4n_hud_scope_draw_padding_block` | 填充 HUD 开镜黑边 | 不同宽高比下的显示 |
| `l4n_game_hud_visible` | HUD 总显示开关 | Necola 已在隐藏准星前读取它 |
| `l4n_vm_sway`/`interp`/`scale` | viewmodel 摆动 | ADS 动画观感和抖动 |
| `l4n_vm_sway_ignore_helpinghand` | 伸手动画时禁用 sway | readme 明确点名可能让 ADS 插件动画失去 sway |
| `l4n_vm_offset_x/y/z` | 全局 viewmodel 偏移 | 与开镜模型自身偏移叠加 |
| `l4n_vm_offset2` | 调整当前 viewmodel 偏移 | 单武器校准 |
| `l4n_pin_viewmodel` | 固定 viewmodel 实体 | 对 ADS 的具体影响未知，需实测 |

“l4nscope 与 Necola 一定双重处理”“一定发生准星冲突”都只是测试假设。正确做法是在相同武器、
相同 MOD 和相同 cvar 组合下分别测试原生 scope、l4nscope 和 Necola ADS。

## 七、模型与资源协议

`mdl_extension.qc` 自称“示例”，可以确认以下扩展语法，但不应称为完整协议：

- `$NekoModel` 示例直接引用 glTF，并展示眼追 flex/flexcontroller 写法；同一文件的 BodyGroup 示例另展示 GLB/FBX，不能据此断言 `$NekoModel` 对三种格式完全等价。
- `l4n` 分组的 flexcontroller 会显示在 L4N 模型选项中。
- BodyGroup 组合受 32 位有符号整数上限约束；超过两个选项的示例使用星号前缀避免静态表情冲突。
- `$KeyValues left4neko` 示例包含用户名称、后缀、足部修正和缩放。
- nekomdl 支持多个 `$TextureGroup`。

其他直接可确认的资源扩展：

- `sequence_event.vdf` 仅针对第一人称武器 viewmodel 动画事件，可重定向或忽略粒子。
- `l4n/scripts/sound/*.txt` 用于扩展声音脚本。
- `l4n/particles/*.pcf` 用于扩展粒子资源。
- nekook 文档只确认高优先级文件系统挂载、免 VPK 测试及部分资产热重载；当前材料不足以支持
  对其注入实现、导出表或底层 Hook 库的结论。

## 八、Necola 对 L4N 的实际依赖

Necola 的核心 ADS Hook 使用 Source 接口和 pattern scan，不调用已知的 L4N 私有函数；但它并非
“完全不依赖 L4N”：

- 依赖 L4N 插件 ABI 被加载并接收生命周期回调。
- 缓存 L4N 上报的模块句柄。
- 通过 `ICvar` 检测并读取 `l4n_game_hud_visible`、`l4n_patch_hud_scope`、
  `l4n_vm_sway` 和 `l4n_vm_sway_ignore_helpinghand`。
- 准星逻辑尊重 L4N HUD 总开关，并直接修改 `crosshair` 根 ConVar。

当前初始化尝试获取 18 个 Source 接口，并在安装前验证当前功能必需的接口和 pattern。随后安装
5 个 Raw hook 组中的 12 个 MinHook detour，另替换 3 个 RecvProp proxy；任何必需步骤失败都会
中止并回滚已安装的 Hook。ADS 状态是 `NONE + LEVEL1..4`，MIXED 是可与这些层级组合的独立状态。

Pattern 失败只能证明特定二进制环境与签名不匹配，不能仅凭失败断言“L4N 修改了 client.dll”。
每次 L4D2、L4N 或关键 MOD 更新后都应重新验证接口版本、pattern 唯一性和模型序列布局。

## 九、安全与生命周期

L4N 插件是游戏进程内的原生 DLL。官方材料没有展示签名校验、权限隔离、沙箱、崩溃隔离或
插件间权限模型，因此安装插件等同于信任其本机原生代码。

官方 readme 把 `-insecure` 描述为用户担忧 VAC 风险时的可选措施，并附带一个明确不使用
`-insecure` 的启动脚本。由此可知 `-insecure` 不是已证实的 L4N 插件加载前置条件，也不能把
它写成 VAC 安全保证。

ABI 没有 shutdown 回调，不等于宿主一定不支持卸载，但当前 Necola 确实不支持安全热卸载：

- 初始化线程句柄立即关闭，无法取消或等待。
- `DLL_PROCESS_DETACH` 在 loader lock 下执行复杂清理。
- 3 个 RecvProp proxy 会在回滚/退出清理时恢复，但控制台命令仍没有注销或释放。
- 控制台命令没有注销或释放。

因此当前部署模型应视为“随进程启动、随进程退出”，不要在运行时对 DLL 执行 `FreeLibrary`。

## 十、仍待验证

1. 修改版 `left4dead2.exe` 的插件枚举、工厂解析和接口版本校验实现。
2. 四个回调的线程、顺序、调用次数、异常边界和实际模块覆盖范围。
3. D3D 回调在原生 D3D9、外置 DXVK 和 `-vulkan` 路径下的对象类型与时序。
4. `key_bind_acts` 对插件晚注册命令的实际接受行为。
5. l4nscope、原生狙击 scope 与 Necola ADS 在不同 HUD/crosshair 配置下的组合结果。
6. `-hide_neko` 时插件目录是否完全跳过，以及宿主是否存在任何显式卸载路径。

补齐这些结论需要目标 L4N 版本的修改版 exe、带版本标识的运行日志和可重复测试矩阵；在此之前，
文档应保留“待验证”标记，而不是把推断写成加载器事实。
