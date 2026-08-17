# CF 动态击杀反馈

## 安装

素材与插件 DLL 分离。把 `CF动态击杀反馈` 中以下两个目录复制到 `<L4D2>/left4dead2/`：

```text
materials/overlays/cf/
sound/cf/
```

不要安装：

```text
addons/sourcemod/plugins/hitsound_v2.smx
```

Necola 已替代该 SMX 的事件、连杀、逐帧动画和声音逻辑。素材约 1.6 GB，不进入插件仓库或 DLL。

## 菜单

打开 `necola_menu`，进入“击杀反馈”：

- 总开关默认关闭。
- “普通感染者”和“特殊感染者（含 Witch）”分别控制目标类型。
- 视觉动画和击杀音效可以独立关闭。
- 普通枪械、爆头、近战、爆炸和连杀效果可以分别关闭；关闭后该类击杀不触发，也不回退普通效果。
- “测试效果”可预览九种外部动画和对应声音。

## 效果映射

| 事件/方式 | 视觉素材 | 声音 |
|---|---|---|
| 普通枪械单杀 | `1kill` | `kill.mp3` |
| 爆头单杀 | `headshot` | `headshot.mp3` |
| 近战单杀 | `meleekill` | `kill.mp3` |
| 爆炸单杀 | `boom` | `grenadekill.mp3` |
| 二至五连杀 | `2kill..5kill` | `multikill_2..5.mp3` |
| 六连杀及以上 | `6kill` | `multikill_6..10.mp3`，十杀后固定最高级 |

当前素材没有步枪、霰弹枪、冲锋枪或狙击枪的独立动画，这些武器统一使用普通枪械效果。

## 运行规则

- 只响应本地玩家造成的击杀。
- `infected_death` 处理普通感染者。
- `player_death` 处理 Smoker、Boomer、Hunter、Spitter、Jockey、Charger 和 Tank。
- `witch_killed` 处理 Witch；其击杀方式由最近一次本地 `infected_hurt` 记录判断。
- 连杀窗口默认 3 秒，回合开始、任务失败和地图过渡时清空。
- 动画共 85 帧，以 30 FPS 播放；新击杀会中断并替换当前动画。
- 素材按原始 2:1 比例居中绘制，避免 16:9 与 16:10 之间直接拉伸。

素材缺失时只停止当前视觉效果，不影响 ADS、菜单或战役计时器。
