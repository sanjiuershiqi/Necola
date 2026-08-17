# cs2hud444 战役计时器

## 用途

原版 `cs2hud444` 的六位时钟通过 VMT `CurrentTime` 材质代理读取当前地图时间，地图切换后会从零
开始。Necola 改为累计已完成小关，并直接控制同一组数字材质的 `$frame`，保留 HUD 原有外观。

## 计时规则

- `LevelInitPostEntity` 后开始当前小关计时。
- 地图加载期间不计时。
- `map_transition` 提交当前小关耗时，下一小关继续累计。
- `mission_lost` 丢弃失败小关耗时，保留此前已完成小关。
- 没有 `map_transition` 的新地图视为新战役并清零。
- 显示范围为 `00:00:00` 到 `99:59:59`，超过 100 小时后小时位从 `00` 循环。

## 安装

需要同时安装匹配版本的 `necola_ads.dll` 和修改后的 HUD：

1. 将 `necola_ads.dll` 放入 `<L4D2>/bin/neko/plugins/`。
2. 用 `cs2hud444_campaign_timer.vpk` 替换原 `cs2hud444.vpk`。
3. 不要同时启用原包和修改包；二者包含相同 HUD 资源，加载优先级不确定。
4. 完全退出并重新启动游戏，使 VMT 和插件重新加载。

当前工作区生成文件位于仓库根目录的 `cs2hud444_campaign_timer.vpk`。该文件由第三方 HUD 重打包，
不提交到仓库；可审查的六个修改源保存在 `cs2hud444_timer_patch/`。

## 命令

| 命令 | 作用 |
|---|---|
| `necola_timer_status` | 输出战役累计、当前小关秒数和地图名 |
| `necola_timer_reset` | 清零并从当前时刻重新开始 |

## 回退

恢复原 `cs2hud444.vpk` 并移除修改版即可恢复 HUD 自带的单地图 `CurrentTime` 计时。插件找不到六个
适配材质时会静默跳过计时显示接管，不影响 ADS 和菜单功能。
