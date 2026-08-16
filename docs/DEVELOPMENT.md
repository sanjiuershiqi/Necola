# 开发指南

> 本文档覆盖构建、调试、日志、部署、故障排查全流程。

## 一、构建

### 1.1 环境要求
| 项 | 要求 |
|---|---|
| OS | Windows（目标平台）；Linux/macOS 仅能编辑代码 |
| 编译器 | MSVC（xmake 自动选择 Visual Studio 的 MSVC） |
| 构建工具 | [xmake](https://xmake.io) |
| 便捷命令 | [just](https://github.com/casey/just)（可选） |
| 架构 | **x86（32位）** — L4D2 是 32 位进程 |
| 依赖 | xmake 自动管理：spdlog、minhook、inipp、nlohmann/json |

### 1.2 首次构建
```bash
# 安装 xmake 后
xmake f -m release -p windows -a x86
xmake
```

产物：`build/windows/x86/release/necola_ads.dll`

### 1.3 用 just 构建
项目根目录有 `justfile`，提供便捷命令：

```bash
just build      # 构建
just install    # 构建 + 复制到 L4D2 plugins 目录
just release    # 构建 + 打包到 release/ 目录
just clean      # 清理 build/
```

**注意**：`justfile` 的 `TARGET`（部署路径）可通过环境变量 `L4D2_PLUGINS` 覆盖：
```bash
L4D2_PLUGINS="D:/steam/.../neko/plugins" just install
```

### 1.4 xmake.lua 要点
见 [xmake.lua](xmake.lua)。关键点：
- `add_files("necola/hook/**.cpp")` — **递归**匹配子目录（曾因用 `*.cpp` 而非 `**.cpp` 导致子目录源码未编译，链接报 20 个未定义符号）
- 依赖通过 xmake 包管理引入，无需手动下载

### 1.5 CI/CD
GitHub Actions 配置见 [.github/workflows/build.yml](.github/workflows/build.yml)。

**触发条件**：push/pull_request 到 main，或手动 workflow_dispatch。

**关键配置**：
- `concurrency`：main 连续推送自动取消旧构建
- 依赖包缓存（spdlog/minhook 编译结果缓存）
- artifact 保留 30 天
- Release 步骤需 `permissions: contents: write`

**常见 CI 失败原因**：
1. 依赖包下载失败（网络问题，重试即可）
2. 链接错误（检查 `**.cpp` glob 是否递归）
3. Release 步骤 403（检查 `permissions: contents: write`）

## 二、部署

### 2.1 标准部署
1. 把 `necola_ads.dll` 复制到 `<L4D2>/neko/plugins/`
2. 确保 L4N 已安装（`<L4D2>/neko/` 目录存在）
3. 启动参数含 `-insecure`（必须，否则插件不加载）

### 2.2 启动参数
推荐的启动参数（见 [kpatch.ini](kpatch.ini) 和 README）：
```
-steam -heapsize 2097151 -tickrate 100 -processheap -high -vulkan -nojoy -insecure -l4n_use_neko_engine_post
```

关键项：
- `-insecure`：允许加载未签名 DLL
- `-l4n_use_neko_engine_post`：启用 L4N 引擎后处理
- `-vulkan`：使用 Vulkan（配合 dxvk）

### 2.3 配置文件
1. **FeatureConfig.json**（主配置）：放 `<L4D2>/neko/FeatureConfig.json`
2. **config.vdf**（L4N 按键绑定）：放 `<L4D2>/neko/config.vdf`，从 `config_template.vdf` 复制改名

## 三、调试与日志

### 3.1 日志文件
- **主日志**：`<L4D2>/L4N-Necola-ADS.log`（spdlog，info 级）
- 无独立的调试日志（v1.4.0 移除了诊断日志系统）

### 3.2 开启详细日志
编辑 `FeatureConfig.json`：
```json
{
    "AdsSupport": { "AdsLog": true },
    "SequenceModify": { "SequenceLog": true }
}
```

**警告**：这两个开关开着时会在**每个动画事件**（`SelectWeightedSequence`、`RecvProxySequence` 等）同步写日志，尸潮时每秒上百行，会导致**明显卡顿**。仅排查问题时临时开启，确认后改回 `false`。

### 3.3 控制台命令（运行时调试）
游戏内按 `~` 打开控制台：
- `necola_menu` — 打开菜单
- `necola_ads` — 切换 ADS
- `crosshair 0/1` — 手动控制准星（验证准星隐藏逻辑）

### 3.4 崩溃排查
游戏崩溃时：
1. 查看 `L4N-Necola-ADS.log` 最后几行，定位崩溃前最后执行的操作
2. 若日志停在某个接口获取，检查对应模块是否加载（`GetModuleHandleA`）
3. 若日志停在偏移解引用，检查 pattern 是否扫描成功（可能 L4N 更新导致 pattern 失效）

**临时恢复诊断日志**：如需详细排查，可在 [Entry.cpp](necola/hook/Entry.cpp) 的 `Load()` 中临时加回 `__try/__except` + 步骤日志（参考 git 历史 commit `39128e0`），排查完毕再移除。

## 四、本地开发流程

### 4.1 迭代流程
1. 修改代码
2. `xmake` 构建
3. 复制 dll 到 `<L4D2>/neko/plugins/`
4. 启动游戏测试
5. 查看日志确认行为

### 4.2 快速部署（just）
```bash
just install   # 构建 + 复制一步到位
```

### 4.3 不要做的事
- **不要**在 `Paint` 钩子里加重逻辑（每帧调用）
- **不要**在动画钩子里无条件打日志（高频调用）
- **不要**在初始化里假设模块已加载（必须等待或判空）
- **不要**用 C++ `catch(...)` 捕获 SEH（需要 `__try/__except`）

## 五、故障排查

### 5.1 游戏启动即崩溃
**检查日志** `L4N-Necola-ADS.log`：

| 日志现象 | 原因 | 修复 |
|---|---|---|
| 无日志生成 | DllMain 都没执行 | 检查 dll 是否在 `neko/plugins/`、L4N 是否启用、`-insecure` 参数 |
| 停在"InitThreadFunc entered" | 模块等待超时 | 检查 7 个关键模块是否加载（见 L4N 研究文档 3.3） |
| 停在 `CreateInterface` | 接口名不匹配 | 引擎版本变化，需重新确认接口名 |
| `SEH exception 0xC0000005` | 偏移解引用失败 | pattern 扫描失败（L4N 修改引擎），加 null 守卫或重新扫描 |

### 5.2 游戏运行中卡顿
**首要排查**：日志开关是否误开。
- 检查 `FeatureConfig.json` 的 `AdsLog` / `SequenceLog` 是否为 `false`
- 若开着，关掉后重启游戏

**次级排查**：菜单是否每帧绘制（正常，但若菜单逻辑过重可优化）。

**对比验证**：临时移除 `necola_ads.dll`，若不卡则确认是本插件问题。

### 5.3 指令无效
- 确认 `necola_menu` 能打开菜单（验证命令注册成功）
- 确认"启用ADS"开关已开（ADS 默认禁用）
- 查看日志是否初始化完成（应有 `Load body completed` 或类似）

### 5.4 按键绑定不生效
1. **L4N config.vdf 方式**：确认 `config.vdf`（非 `config_template.vdf`）的 `key_bind_acts` 段有条目
2. **KeyBinds 方式**：确认 `FeatureConfig.json` 的 `KeyBinds` 数组格式正确
3. 控制台手动 `bind MOUSE3 necola_ads` 验证命令本身有效

### 5.5 历史踩坑速查

| 问题 | 根因 | 修复 commit |
|---|---|---|
| 多线程并发崩溃 | `OnGameLaunch` 和 `OnModuleLoaded` 各启一个线程 | `std::once_flag`（见 dllmain.cpp） |
| 静默崩溃无日志 | SEH 异常 C++ catch 捕获不到 | `__try/__except` 包裹 |
| `m_dwCParticleSystemMgr` 崩溃 | L4N 修改引擎致 pattern 失效返回 0 | null 守卫（见 Entry.cpp Step 3.6） |
| CI 链接错误 20 个未定义符号 | `xmake.lua` 用 `*.cpp` 不递归 | 改为 `**.cpp` |
| CI 不触发 | workflow 在非默认分支 | 合并到 main |
| CI Release 403 | 缺 `contents: write` 权限 | 加 `permissions` |
| 画面一卡一卡 | `adsLog`/`sequenceLog` 误开 | 默认 false，排查后关闭 |

## 六、版本发布

### 6.1 发 Release
1. 确认 main 分支 CI 构建通过
2. 打 tag：`git tag v1.4.1 && git push origin v1.4.1`
3. GitHub Actions 自动构建并创建 Release
4. 或从 Actions 下载 artifact 手动上传

### 6.2 版本号规则
- 更新 [necola/vars.h](necola/vars.h) 的 `sFixVer`
- 主版本号：破坏性改动
- 次版本号：新功能
- 修订号：bug 修复

## 七、适配新引擎/L4N 版本

当 L4D2 或 L4N 更新导致插件失效时：

1. **重新扫描 pattern**：用 IDA Pro/Ghidra 打开新的 `client.dll`，对比 [Offsets.cpp](necola/sdk/Offsets.cpp) 的 pattern 是否还能匹配
2. **验证接口名**：检查引擎 DLL 的 `CreateInterface` 导出表，确认接口名（如 `VEngineClient013`）是否变化
3. **更新偏移**：pattern 失效的重新提取字节模式
4. **测试**：重点测试 SEH 异常是否出现

详见 [L4N_PLUGIN_RESEARCH.md](L4N_PLUGIN_RESEARCH.md) 第 5 节。
