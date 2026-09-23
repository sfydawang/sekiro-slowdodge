# Sekiro: Slow-Motion on Successful Dodge (demo v0.1)

只狼（Sekiro: Shadows Die Twice, PC 1.06 Worldwide）的一个玩法 Mod：**成功闪避的瞬间给一段"缓速时间"**，
给玩家一个"考虑下一步"的窗口——打攻击、放技能、再闪，或者什么都不做。

> 这是 **demo/实验版**，已知问题见文末。请务必先读"离线警告"。

## 它做什么

- **成功闪避 = 真的躲掉一次本来会砍中你的攻击**（判定帧 + 射程内 + 全程没受伤）。
  距离按**按下垫步那一刻**算，不是闪完之后拉开的距离。
- 闪避成功后：**世界 0.25 倍速 / 最多 1 秒（现实时间）**。
  - 你按下攻击 / 技能 / 再闪 → **那一瞬缓速结束**，游戏回归正常；
  - 什么都不做 → 1 秒到点结束。
- **缓速期间你按下的动作按 2 倍速播放**（"突破限制"）：垫步的动作锁定不再被缓速拖长，
  所以你能在窗口里真的接出攻击/技能。
- **无敌规则**：触发瞬间无无敌；**缓速期间全程无敌**；缓速正常结束后再给 **0.25 秒**无敌
  （用来吃那些垫步躲不掉的攻击）。除此之外没有任何额外无敌。
- **取消规则**：缓速期间挨刀 / 零伤害的受击动作（如义父的踩）/ 被弄上天 / 投技 → 缓速立刻作废；
  毒与持续伤不算受击，不会取消。
- **画面**：像"时间穿梭"的镜头特写——从画面中心向四周轻微拉长 + 一点点推近，出现 120ms、结束 420ms 淡出。
- **范围自适配**：按每一招的实测射程学习（门限 = 实测 + 余量），另有 `tight` / `loose` 两档可调。

## Features 

- **Successful dodge → slow motion**: after you truly evade an attack that would have hit you, the world drops to **0.25x** for up to **1 second** (real time).
- **You pick the next move**: press **attack / combat art / dodge again** and the slow ends **instantly**; do nothing and it ends by itself after 1s.
- **Action unlock**: during the slow your own actions play at **2x**, so the dodge's action-lock no longer drags — you can really follow up with an attack or a prosthetic (e.g. Shadowrush).
- **Invincibility**: **none** at the trigger; **full invincibility during the slow**; plus **0.25s** after a normal end (covers hits a step-dodge cannot avoid).
- **Cancel rules**: taking a hit / a **zero-damage hit reaction** (e.g. Owl's stomp) / being launched / **grabs** end the slow immediately; poison and DoT do **not** cancel it.
- **Strict by design**: the attack had to be going to connect (hit frames + within reach + no damage taken). Distance is measured **at the moment you press dodge**, so sideways and backward dodges count too.
- **Self-adapting range**: per-move reach is learned from what actually hits you; `tight` / `loose` presets available.
- **Subtle visual**: a light radial "time-warp" stretch from the screen center (120ms in / 420ms out), disable with `nofx`.

> Status: **demo**. Known limitations are listed below — in particular there is **no audio** (see below why).
## 安装（需要 me3）

1. 安装 [me3](https://github.com/garyttierney/me3)。
2. 在本仓库 **Releases** 下载 `slowdodge.dll`，放到 me3 的 sekiro-mods 目录下。
3. 用 me3 启动 Sekiro（`me3 launch -g sekiro`），或把 DLL 加进你的 me3 profile。

> 本 demo 只用 me3 的**正常加载**方式；本仓库不附带任何启动器。
> 编译：见 `tools/build.bat`（需要 mingw-w64 的 g++）。

## ⚠️ 离线警告（必读）

**请只在离线模式下游玩。** 这个 Mod 会修改游戏进程内存、可能影响联机校验，
联机游玩有被 FromSoftware **软 Ban**（联机封禁）的风险。由此产生的后果自负。

## 已知问题（demo 版如实列出）

1. **触发会漏判**：约 10~15% 的"闪避成功"没能起缓速（起手动画认不出的招靠"回溯判定"兜，
   仍有边界情况）。
2. **没有音效**：我们尝试过直接放慢游戏音频（FMOD），但 FMOD 的对象必须由游戏自己的线程访问，
   从外部线程调用会崩溃（试过 4 次）、打代码补丁也会崩（3 次），因此**发布版不含任何音频注入**。
3. **"取消链"（技能被垫步取消 → 强化缓速、技能可被打断）未启用**：判定不可靠，默认关闭。
4. **特效可能与叠加层冲突**：缓速特效挂的是 D3D11 的 `Present`，和 Steam 覆盖层 / ReShade /
   MSI Afterburner 之类可能冲突。出问题先把特效关掉。
5. **只在一台机器 + 一个游戏版本上验证过**（1.06 Worldwide）；版本不符时启动日志里会写
   `SELFCHECK ... 签名[...]` 并跳过对应补丁。

## 运行配置

Mod 读取 EXE 同目录/桌面上的 `MODE.txt`（一行字符串，可拼关键字）：

| 关键字 | 作用 |
|---|---|
| `ps2` / `ps15` / `ps1` / `ps3` / `ps4` | 缓速期间玩家自己的动作倍速（默认 1.5，本 demo 建议 `ps2`） |
| `loose` / `tight` | 判定范围三档（默认 / 更宽 / 更紧） |
| `if` | 保留旧的"钉住无敌帧"（默认关） |
| `oldif` | 恢复旧的 2.2 秒保护（默认关，仅做对比） |
| `nofx` | 关闭画面特效 |
| `sfxsting` | 打开提示音（默认关） |

日志写在桌面 `slow.txt`（**这是本 demo 的已知不便，后续会改成与 DLL 同目录**）。

## 数据来源与致谢

- `src/sekiro_events.h`（攻击判定帧 / 放弹丸 / 投技）与 `src/sekiro_iframes.h`（玩家无敌帧）
  是**作者用自己本机的游戏档案只读解析生成的**（脚本在 `tools/`）。
- `src/sekiro_tables.h` 为**空表**：原内容来自第三方仓库
  [thisguymartin/sekiro-deflect-observer](https://github.com/thisguymartin/sekiro-deflect-observer)，
  按"只发布自有数据"的原则未包含；发布版因此少了"社区表窗口"这一条判据（功能不受影响）。
- 解析过程中参考了社区公开的 **TAE 事件结构**与 **Paramdex** 的字段释义（仅作说明，未随仓库分发）。
- 本仓库不包含游戏原始素材（模型/贴图/音频）与任何第三方音效文件。

## 许可

MIT（见 `LICENSE`）。
