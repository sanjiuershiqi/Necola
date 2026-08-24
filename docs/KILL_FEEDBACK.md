# Skeeto 主题命中与击杀反馈

## 安装

击杀反馈使用 skeeto 的 overlay、屏幕/世界粒子与音效，全部来自外部 addon 包 `skeeto_killfeed.vpk`
（位于 `Necola_analysis/addons/`）：

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
- `player_death` 是 Smoker、Boomer、Hunter、Spitter、Jockey、Charger、Tank 和 Witch 的权威来源；
  各特感专用死亡事件仅作延迟回退，`witch_killed` 只清理 Witch 状态。
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
- `particle`/`particles[]` 使用 skeeto 原版 `DispatchParticleEffect` 通道；CF、Valorant 的击杀/连杀
  是屏幕粒子，落雷、爱心、星星和 Advanced 主题按 `world=true` 在目标位置生成。粒子在实际触发时
  按需预热，不在地图载入期间批量扫描或预热 VPK。
- 初始化时按 skeeto 的 `Cmd_PlaySound` 逻辑一次性解锁 `r_screenoverlay`、`play`、`playvol`：清除
  `FCVAR_CHEAT` 并添加 `FCVAR_CLIENTCMD_CAN_EXECUTE`，因此远程服务器也能本地显示和播放。

素材缺失时不会影响 ADS、菜单或战役计时器；可通过“测试效果”逐项检查 `skeeto_killfeed.vpk`
是否安装完整。
