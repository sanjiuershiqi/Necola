# CF 动态击杀反馈（skeeto 素材版）

## 安装

击杀反馈使用 skeeto 的 overlay、屏幕/世界粒子与音效，全部来自外部 addon 包 `skeeto_killfeed.vpk`
（位于 `Necola_analysis/addons/`）：

1. 把 `skeeto_killfeed.vpk` 复制到 `<L4D2>/left4dead2/addons/`，游戏会自动挂载。
2. 将 `necola_ads.dll` 放入 `<L4D2>/bin/neko/plugins/`。
3. 完全退出并重启游戏。

不再需要旧的 `materials/overlays/cf/`、`sound/cf/` 目录和
`addons/sourcemod/plugins/hitsound_v2.smx`；如已安装请删除。素材不进入插件仓库或 DLL。

## 菜单

打开 `necola_menu`，进入“击杀反馈”：

- 总开关默认关闭。
- “普通感染者”和“特殊感染者（含 Witch）”分别控制目标类型。
- “特感分类设置”可分别控制 Smoker、Boomer、Hunter、Spitter、Jockey、Charger、Tank 和 Witch。
- 图标（含粒子）和音效可以独立关闭；主题选项会直接切换 VPK 中不同的视觉/音效配置。
- “命中提示”提供 0/1/2 三态：关闭、仅特感命中、全部命中；并可切换 SI 视觉优先、SI 音效优先。
- 音效音量可设为 0/25/50/75/100%。
- 普通枪械、爆头、近战、爆炸和连杀效果可以分别关闭；关闭后该类击杀不触发，也不回退普通效果。
- “测试效果”可预览八种图标与声音（普通/特感的击杀、爆头、近战，特感三连与十连杀）。

## 效果映射（skeeto/cf 主题）

| 事件/方式 | 视觉材质 | 声音 |
|---|---|---|
| 普通感染者击杀 | `skeeto/ci/cf/kill` | `sound/skeeto/ci/cf/kill.mp3` |
| 普通感染者爆头 | `skeeto/ci/cf/headshot` | `sound/skeeto/ci/cf/headshot.mp3` |
| 普通感染者近战 | `skeeto/ci/cf/melee` | `sound/skeeto/ci/cf/melee.mp3` |
| 特感击杀 | SI/CI 候选按“SI视觉优先”合并 | SI/CI 标量声音按“SI音效优先”合并 |
| 特感连杀 | SI JSON 的 `streak_N` / `streak_default` | 对应 SI 主题声音 |
| 特感爆头/近战 | SI 和 CI 候选各自按 priority 选择后再合并 | 同上 |
| 特感爆炸击杀 | 回退当前连杀图标 | 回退对应连杀声音 |

样式优先级与 skeeto 一致：候选内部按 `streak/kill -> melee -> headshot` 顺序、严格比较 priority；
SI/CI 候选之间不比较 priority，而由“SI视觉优先”和“SI音效优先”分别合并。普通感染者不参与
SI 连杀计数；特感 streak 是滚动计数并按主题 `wrap` 循环，切换 SI 主题或回合重置时归零。

## 运行规则

- 只响应本地玩家造成的击杀。
- `infected_death` 处理普通感染者。
- `player_death` 处理 Smoker、Boomer、Hunter、Spitter、Jockey、Charger 和 Tank。
- `witch_killed` 处理 Witch；其击杀方式由最近一次本地 `infected_hurt` 记录判断。
- 特感普通枪械和爆头增加 streak；近战与爆炸使用专用/普通 kill 样式，不增加也不重置 streak。
- 图标使用 skeeto 原版 `r_screenoverlay` 路径：击杀默认显示 240ms，命中默认 110ms，主题 JSON
  可通过 `overlay_ms` 覆盖；不同 overlay 之间有 70ms 节流，到期自动执行 `r_screenoverlay off`。
- `particle`/`particles[]` 使用 skeeto 原版 `DispatchParticleEffect` 通道；CF、Valorant 的击杀/连杀
  是屏幕粒子，落雷、爱心、星星和 Advanced 主题按 `world=true` 在目标位置生成。进图后会预热
  当前 SI/CI 主题粒子，避免首次击杀缺图标。
- 初始化时按 skeeto 的 `Cmd_PlaySound` 逻辑一次性解锁 `r_screenoverlay`、`play`、`playvol`：清除
  `FCVAR_CHEAT` 并添加 `FCVAR_CLIENTCMD_CAN_EXECUTE`，因此远程服务器也能本地显示和播放。

素材缺失时不会影响 ADS、菜单或战役计时器；可通过“测试效果”逐项检查 `skeeto_killfeed.vpk`
是否安装完整。
