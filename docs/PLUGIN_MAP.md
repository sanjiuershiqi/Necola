# 插件完整地图（结构审计）

> 机械提取 + 代码核对，非推测。行数为 `Get-Content | Measure-Object -Line` 实测。

## 1. 产物与构建

- 目标：`necola_ads.dll`（x86，C++20），放在 `<L4D2>/bin/neko/plugins/`
- 构建：`xmake`（`xmake.lua`），CI `.github/workflows/build.yml`（x86 release + 产物上传）
- 平台契约：L4N 插件 ABI **v2**（`necola/l4n_plugin.h`）——`IL4NPluginV1` + `IL4NPlugin::RequestHudMenu`

## 2. 模块与规模

| 模块 | 行数 | 职责 |
|---|---|---|
| AdsSupport | 2101 | ADS 状态机、动画重映射、每武器准星（**本次不动的部分**） |
| MenuManager | 1448 | 自绘 VGUI 菜单（7 项/页） |
| SequenceModify | 1364 | 序列修正（ADS 底层，RecvProp 代理） |
| KillFeedback | 1147 | 主题命中/击杀特效 + fx 键 + 击杀检测（已重写） |
| DamagePipeline | 377 | 命中检测管线（已重写，Skeeto 三段式） |
| HitFeedback | 251 | 伤害数字 + 准星标记（纯渲染，已重写） |
| CampaignTimer | 202 | 战役计时 + 材质接管 |
| InputManager | 192 | 窗口输入/热键 |
| ParticlePlatform | 159 | 粒子清单注入（Skeeto 粒子平台移植） |
| L4NMenu | 147 | L4N HUD 菜单桥接（`RequestHudMenu` → KeyValues） |
| BodygroupFix | 109 | bodygroup 修复 |
| CommandManager | 40 | 控制台命令注册 |

## 3. 加载时序（`Entry.cpp`，日志 `Step N`）

1. `U::Offsets.Init()` —— pattern 扫描（client.dll）
2. `G::Vars.Load()`
3. CreateInterface（14 个接口）+ `L4N::Env.Init()`
3.5 偏移解引用（ClientMode / GlobalVars / 粒子）
4. `G::InputManagerI.Init()`
5. `G::Hooks.Init()`（MinHook 初始化 + 全部 detour）
5.5 **`ParticlePlatform::Install()`** ← 粒子清单钩子
6. 配置加载（`AdsSupport` / `SequenceModify` / `MenuManager` / `KillFeedback` / `HitFeedback`）
7. `SModify.RecvPropDataHook()`
8. `AdsMgr.Init()`（若启用）
9. `MenuMgr` 初始化
10. 命令注册（见 §6）
11. `KillFeedback.InitListeners()`
12. `DamagePipeline::InitListeners()`（含 `InstallHealthProxy()`）
    卸载：`Undo_necola()` 逆序，含 `ParticlePlatform::Shutdown()`

## 4. Raw hook 点

| 文件 | detour 目标 | 用途 |
|---|---|---|
| BaseClient | `LevelInitPreEntity` / `LevelInitPostEntity` / `LevelShutdown` / `FrameStageNotify` / `IN_KeyEvent` | 地图生命周期、帧阶段、菜单按键 |
| BaseAnimating | `SetSequence` + 4 个 RecvProxy（seq VM/activity 等） | ADS 动画重映射、序列修正 |
| BaseCombatWeapon | 2 个武器方法（主/副攻击与动画） | ADS 检视/切枪行为 |
| EngineVGui | `Paint` | 每帧绘制入口（KillFeedback.Draw / DamagePipeline.Pump / HitFeedback.Draw / 菜单） |
| GameEventManager | `FireEvent` | 事件入口（可选拦截） |

## 5. 事件订阅

| 订阅者 | 事件 |
|---|---|
| KillFeedback | `round_start`/`round_end`/`mission_lost`/`map_transition`、`player_hurt`、`infected_hurt`、`melee_kill`、`player_spawn`、`player_death`、`zombie_death`、`infected_death`、`witch_killed`、`lunge_pounce`/`pounce_stopped`/`jockey_ride`/`jockey_ride_end`、`boomer_exploded`/`charger_killed`/`spitter_killed`/`jockey_killed`/`tank_killed` |
| DamagePipeline | `player_hurt`、`infected_hurt`、`player_spawn`、`round_start`/`round_end`/`mission_lost`/`map_transition` |

## 6. 命令

`necola_menu`、`necola_ads`、`necola_ads_mixed`、`necola_ads_foreceback`、`necola_ads_back`、
`necola_timer_reset`、`necola_timer_status`、`necola_killfeedback_status`、`necola_killfeedback_test`、`necola_hitfeedback_status`

## 7. 配置段（`FeatureConfigManager`）

`AdsSupport` / `SequenceModify` / `KillFeedback` / `HitFeedback`（`MenuManager` 的外观值也在配置里）

## 8. 数据流

```
命中：player_hurt / infected_hurt ─┐
      m_iHealth 变化（RecvProp 代理）├→ DamagePipeline 队列(64)
                                     │      帧内 drain(≤32)：
                                     │      地图代次 → 400ms 时效 → userid→实体
                                     │      → 每玩家世代戳 → 血量字段比对 → 门控
                                     │      （远锥5000/0.93 → 70ms → 近锥110/0.90 → 400ms 受害者锁）
                                     ├→ HitFeedback.AddNumber()/FlashMarker()（数字 + 准星）
                                     └→ KillFeedback.OnSpecialHealthDrop()（主题命中，110ms）

击杀：player_death / zombie_death / infected_death / witch_killed / 专用事件
        → KillFeedback.HandleKill/Trigger（本地判定 = SKT_Evt_InfectedDeath 三规则）
        → 主题样式竞争（streak_N → kill / melee / headshot，按 priority）
        → PlayStyle：overlay(+序列) / 粒子(32/s) / 音效；击杀 240ms
      fx：witch_killed → DetectCrown（melee_crown/perfect_crown/crown）
          player_death → MatchFxKey（skeet/melee_skeet/headshot_skeet/crit_text）
```

## 9. 菜单与 L4N 集成

- 自绘 VGUI：`MenuManager`（`necola_menu` 切换），主题列表 = 内置表 + 运行时发现（`skeeto_*.json` 扫描）
- L4N 原生菜单：`L4NMenu` 生成 KeyValues，`RequestHudMenu(true)` 返回标题、`(false)` 返回树；回调按 `user_data` 整数翻转开关，回传 `_refresh` 原地刷新

## 10. 已知缺口 / 风险

1. `ParticlePlatform`：钩子是 **引擎清单解析器**（MinHook），若目标引擎槽位/序言不同，会走"未装钩子"分支（已日志化），不会崩
2. fx 的世界坐标未传入 `PlayStyle`：皇冠/skeet 的世界粒子落在本地玩家原点
3. 近战 Charger 的 `level` fx 键未实现（依赖 SKT 的 charger-pummel 状态机）
4. 主题的 `si_*`/`ci_*` 选择仍靠菜单；`_siTheme` 默认 `si_cf`
5. `MenuManager` 1448 行自绘代码与 `L4NMenu` 并存（迁移到 L4N 菜单只做了第一页集合）
6. ADS / SequenceModify 为本次未改动的既有实现，行为沿用原样
