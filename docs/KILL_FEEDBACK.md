# CF 动态击杀反馈（skeeto 素材版）

## 安装

击杀反馈使用 skeeto 的单帧图标与音效，全部来自外部 addon 包 `skeeto_killfeed.vpk`
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
- 视觉动画和击杀音效可以独立关闭。
- 普通枪械、爆头、近战、爆炸和连杀效果可以分别关闭；关闭后该类击杀不触发，也不回退普通效果。
- “测试效果”可预览八种图标与声音（普通/特感的击杀、爆头、近战，特感三连与十连杀）。

## 效果映射（skeeto/cf 主题）

| 事件/方式 | 视觉材质 | 声音 |
|---|---|---|
| 普通感染者击杀 | `skeeto/ci/cf/kill` | `sound/skeeto/ci/cf/kill.mp3` |
| 普通感染者爆头 | `skeeto/ci/cf/headshot` | `sound/skeeto/ci/cf/headshot.mp3` |
| 普通感染者近战 | `skeeto/ci/cf/melee` | `sound/skeeto/ci/cf/melee.mp3` |
| 特感击杀（含连杀第 1 杀） | `skeeto/si/cf/1kill` | `sound/skeeto/si/cf/1kill.mp3` |
| 特感 2~10 连杀 | `skeeto/si/cf/2kill..10kill` | `sound/skeeto/si/cf/2kill..10kill.mp3` |
| 特感爆头击杀 | `skeeto/si/cf/headshot` | `sound/skeeto/si/cf/headshot.mp3` |
| 特感近战击杀 | `skeeto/si/cf/knifed` | `sound/skeeto/si/cf/knifed.mp3` |
| 特感爆炸击杀 | 回退当前连杀图标 | 回退对应连杀声音 |

优先级与 skeeto 主题一致：近战 > 爆头 > 连杀 > 普通击杀。普通感染者不参与连杀计数；
特感连杀窗口默认 3 秒，近战与爆炸不改变连杀计数。

## 运行规则

- 只响应本地玩家造成的击杀。
- `infected_death` 处理普通感染者。
- `player_death` 处理 Smoker、Boomer、Hunter、Spitter、Jockey、Charger 和 Tank。
- `witch_killed` 处理 Witch；其击杀方式由最近一次本地 `infected_hurt` 记录判断。
- 连杀窗口默认 3 秒，回合开始、任务失败和地图过渡时清空。
- 只有普通枪械和爆头计入连杀；近战与爆炸始终显示各自图标，不增加也不重置现有连杀计数。
- 图标为单帧全屏素材，每次显示 1 秒，按素材原始 2:1 比例居中绘制，不占用全局 `r_screenoverlay`。
- 当屏幕宽度超过 2048 时保持 2048 宽度上限，避免将素材放大到 4K 后变模糊。

素材缺失时不会影响 ADS、菜单或战役计时器；可通过“测试效果”逐项检查 `skeeto_killfeed.vpk`
是否安装完整。
