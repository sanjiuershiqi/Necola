# Skeeto 主题命中与击杀反馈

## 安装

击杀反馈使用 skeeto 的 overlay、屏幕/世界粒子与音效，全部来自**外部 addon 包** `skeeto_killfeed.vpk`
（素材不随本仓库分发、也不进入 DLL；请自行取得后按下述步骤安装）：

> **主题发现方式已改成 skeeto 那种**：插件在运行期**通配枚举文件系统**
> `skeeto/skeeto_*.json`（松散目录 + 所有已挂载的 VPK），而不是信任写死的 17 个主题 id。
> ⇒ 任何 addon 只要把 `skeeto/skeeto_<id>.json` 打进 VPK，就会自动出现在主题列表里；
> 与 skeeto 相同，**同一 id 后读入者覆盖先读入者**（松散先扫、VPK 后扫），所以旧包里
> 的同 id 主题会盖掉你调试用的松散版本。
> 枚举通过 `IFileSystem::FindFirst/FindNext/FindClose` 完成，并用 SEH 包裹：如果目标引擎
> 这组虚表槽位与随附 SDK 头不一致，只会退回内置列表并写日志，不会崩进程。


1. 把 `skeeto_killfeed.vpk` 复制到 `<L4D2>/left4dead2/addons/`，游戏会自动挂载。
2. 将 `necola_ads.dll` 放入 `<L4D2>/bin/neko/plugins/`。
3. 完全退出并重启游戏。

不再需要旧的 `materials/overlays/cf/`、`sound/cf/` 目录和
`addons/sourcemod/plugins/hitsound_v2.smx`；如已安装请删除。素材不进入插件仓库或 DLL。

## 菜单

打开 `necola_menu`，进入“主题反馈”：

- “启用击杀提示”只控制击杀，不控制主题命中。
- “普通感染者”和“特殊感染者（含 Witch）”分别控制目标类型。
- “特感分类设置”可分别控制 Smoker、Boomer、Hunter、Spitter、Jockey、Charger、Tank 和 Witch。
- 图标（含粒子）和音效可以独立关闭；主题选项会直接切换 VPK 中不同的视觉/音效配置。
- “主题命中效果”独立提供 0/1/2 三态：关闭、仅特感命中、全部命中；不依赖击杀提示开关，
  并可切换 SI 视觉优先、SI 音效优先。
- 音效音量可设为 0/25/50/75/100%。
- 普通枪械、爆头、近战、爆炸和连杀效果可以分别关闭；关闭后该类击杀不触发，也不回退普通效果。
- “测试效果”可预览八种图标与声音（普通/特感的击杀、爆头、近战，特感三连与十连杀）。
- 常用的 SI/CI 主题、主题命中、视觉、声音和音量集中在第一页；主题选择器显示友好名称并以绿色
  `[当前]` 标记当前项。特感分类页提供全部开启/关闭，击杀方式页提供一键全部开启。

根菜单“伤害数字与准星”是另一套独立 VGUI 功能，只控制浮动伤害数字和红色准星命中标记；它既不
开启也不关闭主题命中 overlay、粒子或声音。

## 效果映射（skeeto/cf 主题）

| 事件/方式 | 视觉材质 | 声音 |
|---|---|---|
| 普通感染者击杀 | `skeeto/ci/cf/kill` | `sound/skeeto/ci/cf/kill.mp3` |
| 普通感染者爆头 | `skeeto/ci/cf/headshot` | `sound/skeeto/ci/cf/headshot.mp3` |
| 普通感染者近战 | `skeeto/ci/cf/melee` | `sound/skeeto/ci/cf/melee.mp3` |
| 特感击杀 | SI/CI 候选按“SI视觉优先”合并 | SI/CI 标量声音按“SI音效优先”合并 |
| 特感连杀 | SI JSON 的 `streak_N` / `streak_default` | 对应 SI 主题声音 |
| 特感爆头/近战 | SI 和 CI 候选各自按 priority 选择后再合并 | 同上 |
| 特感爆炸/燃烧击杀 | 没有专属页面，使用当前 streak/base kill | 对应主题声音 |

样式优先级与 skeeto 一致：候选内部按 `streak/kill -> melee -> headshot` 顺序、严格比较 priority；
SI/CI 候选之间不比较 priority，而由“SI视觉优先”和“SI音效优先”分别合并。普通感染者不参与
SI 连杀计数；特感 streak 是滚动计数并按主题 `wrap` 循环，切换 SI 主题或回合重置时归零。

## 运行规则

- 只响应本地玩家造成的击杀。
- `infected_death` 处理普通感染者。
- `player_death` 是 Smoker、Boomer、Hunter、Spitter、Jockey、Charger、Tank 的权威来源；各特感专用
  死亡事件仅作延迟回退。
- **Witch 走 `witch_killed`**：Witch 不是玩家实体，不会有 `player_death`，所以这个事件本身就是完整
  击杀路径（与 skeeto 的 `witch_killed` 分支一致）：校验 `userid` 是本地玩家 → 过
  `killFeedbackSpecial` + `killFeedbackWitch` 两级开关与击杀方式过滤 → 走 **CI 通道**（不增加 SI
  连杀计数），并与同帧的 `infected_death` 去重。
- 爆头、近战和爆炸是正交分类；爆头与近战可同时成立，再由主题 priority 决定最终页面。电锯和全部
  标准近战武器名都按 melee 识别，爆炸/燃烧没有专属页面时仍会触发 base kill/streak。
- 每个本地 SI/Witch `player_death` 都增加 streak，包括近战、爆炸和燃烧；普通感染者不参与。
- 特感非致死命中使用客户端 `player_hurt`；不在地图实体创建阶段安装额外健康 RecvProxy，避免与
  L4N/其他插件的 RecvProp 链冲突。远程服务器若不转发该本地事件，击杀提示仍正常，但逐次 SI 命中
  可能不可用。
- `HitMode=2` 的普通感染者/Witch 命中使用 `bullet_impact`、45ms 延迟和 `EngineTrace` 确认目标，
  `infected_hurt` 仅作为本地补充。
- 图标使用 skeeto 原版 `r_screenoverlay` 路径：击杀默认显示 240ms，命中默认 110ms，主题 JSON
  可通过 `overlay_ms` 覆盖；不同命中 overlay 之间有 70ms 节流，到期执行 `r_screenoverlay ""`。
- **多帧序列**：主题 JSON 的 `overlay_sequence[]` + `overlay_step_ms` 已支持（这是 skeeto 的
  材质动画写法）。按 `overlay_step_ms`（钳到 16–1000ms）逐帧推进，最多 30 帧；若 `overlay_ms`
  短于 `帧数 × step`，会**自动延长**到足够播完最后一帧（与 skeeto 的规则一致）。只有单帧
  `overlay` 而没有序列的主题，行为与以前完全相同。
- 样式字段按 skeeto 事件块的上限截断：序列 **30** 帧、`particles[]` **8** 条、`sounds[]` **8** 条、
  `overlay_ms` **≤5000ms**。超出部分静默丢弃，不会让整份主题失效。
- `particle`/`particles[]` 使用 skeeto 原版 `DispatchParticleEffect` 通道；CF、Valorant 的击杀/连杀
  是屏幕粒子，落雷、爱心、星星和 Advanced 主题按 `world=true` 在目标位置生成。粒子**在主题选定
  时预热该主题引用的系统**（不在整库/整 VPK 批量扫描），因此第一次击杀不会再因为"边派发边预热"
  而丢帧或丢特效；主题切换时也会立即预热新主题。
- **粒子限流**：与 skeeto 的 `SKT_Particle_Dispatch` 一致，**1000ms 窗口内最多派发 32 次**，
  超出的请求直接丢弃并写一行日志；多杀不会再堆叠出几十个特效。
- **世界坐标回退链**：`world=true` 时依次取 事件 `victim_x/y/z` → 目标实体原点 → 上次
  `bullet_impact`；三者都缺失时回退到**本地玩家原点**（此前会落在 `(0,0,0)`，即地图原点／虚空）。
- **预热诊断**：`PrecacheParticleSystem` 的返回索引会写进日志
  （`particle precached <name> (index=N warmup=0/1)`）。`index <= 0` 基本等于"该系统没注册"，
  通常意味着主题的 pcf 缺失或 `skeeto_killfeed.vpk` 未挂载——排障先看这一行。
- 命令解锁：只对 `r_screenoverlay` 清 `FCVAR_CHEAT` 并加 `FCVAR_CLIENTCMD_CAN_EXECUTE`（与
  `UnlockCommands()` 一致）。**音效不走 `play`/`playvol`**：优先 `IMatSystemSurface::PlaySound`
  （本地 UI 采样，不碰 sound-script 缓存），显式音量时走 `IEngineSound::EmitAmbientSound`，
  两者都不可用才回退到控制台命令。

素材缺失时不会影响 ADS、菜单或战役计时器；可通过“测试效果”逐项检查 `skeeto_killfeed.vpk`
是否安装完整。
