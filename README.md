# Refined Armour Penetration (AP Overhaul)

> **Total War: WARHAMMER III** native combat formula overhaul — **Windows (x86_64) Only**  
> Supports **Mod Configuration Tool (MCT)** & **One-Click Steam Workshop Subscribe** (auto-extracts native DLL from `.pack`).

---

## English

### Overview

In vanilla WARHAMMER III, Armour-Piercing (AP) damage `A` completely ignores armour, while non-AP damage `N` is reduced independently (`N * (1 - red) + A`). This leads to two well-known issues:
1. Low-AP elite units (e.g. heavy cavalry or non-AP infantry) struggle disproportionately against high-armour targets.
2. High-armour targets (`100+` Armour) suffer from **armour overflow** on the non-AP portion — once the random roll exceeds `100%`, extra armour hits a hard wall and yields rapidly diminishing returns.

**Refined Armour Penetration** replaces the vanilla two-part armour resolution at all three battle damage sites (melee/collision struct, and both integer missile/direct damage sites) with a **unified proportional penetration model** and a smooth high-armour transition.

### Armour Algorithm

Let:
- `N` = Base (non-AP) damage
- `A` = Armour-Piercing (AP) damage
- `D = N + A` = Total raw damage
- `armour` = Target's armour value

The formula computes:
1. **AP Ratio**: `r = clamp(A / D, 0, 1)`
2. **Penetration Factor**: `p = r ^ gamma` (default `gamma = 1.30`)
3. **Effective Armour**: `eff_armour = armour * (1 - p)`
4. **Two Reduction Curves**:
   - **Refined Linear Roll (Uncapped)**: `roll = uniform(eff_armour * lower_cap, eff_armour)`, `red_lin = clamp(roll * 0.01, 0, 1)` (default `lower_cap = 0.50`)
   - **Hyperbolic Curve**: `red_hyp = eff_armour / (eff_armour + S)` (default `S = 20`)
5. **Mode Selection (configurable in MCT)**:
   - **`Combined` (Default)**: `red = min(red_lin, red_hyp)`
   - **`Refined Linear Only`**: `red = red_lin`
   - **`Hyperbolic Only`**: `red = red_hyp`
6. **Final Damage**: `dmg = max(min_damage, D * (1 - red))` (default `min_damage = 1.0`)

### Understanding `gamma`, `Combined` Mode & Combat Feel

- **Why `min(Refined Linear, Hyperbolic(20))`?**
  Instead of imposing an abrupt hard cap (like a flat 85% wall), taking `min(red_lin, red_hyp)` preserves the tactile **linear-roll** curve across low/mid effective armour (`0 – 100`), and then **smoothly transitions into `Hyperbolic(S = 20)`** at high effective armour (`110+`). Notice that because `red_lin` is a random roll in `[0.5*eff, 1.0*eff]`, its upper rolls begin touching `red_hyp` slightly earlier (~`eff = 70–80`), creating a completely seamless, kink-free transition into diminishing returns.
- **`gamma = 1.00` (Mathematically Same Expected Damage as Vanilla below 100 Armour, Different Variance)**:
  When `gamma = 1.0` and armour does not overflow (`armour <= 100`), `eff_armour = armour * (N / D)`, so the average damage reduction is `0.75 * armour * 0.01 * (N / D)`. Multiplying by total damage `D` yields `D - N * (0.75 * armour * 0.01) = N * (1 - 0.75 * armour * 0.01) + A`, which has **the exact same expected damage as vanilla**.
  However, **the single-hit distribution is different**: vanilla rolls RNG only on `N` while `A` is deterministic, whereas the refined model rolls RNG across total damage `D` using `eff_armour`.
- **`gamma = 1.30` (Default — Tactile Heavy-Armour Feel)**:
  Slightly curves penetration downward for mixed/low-AP weapons (`50%` AP -> `40.6%` penetration instead of `50%`). Heavy armour (`80–140+`) feels noticeably sturdier against generalists and chaff, while dedicated AP weapons (`70%+` AP) retain clear, decisive authority.
- **`gamma = 1.40+` (Hardcore Armour Differentiation)**:
  Widens the gap between true AP specialists and hybrid weapons (`50%` AP -> `37.9%` penetration).

### Solving Vanilla Armour Overflow (`100+` Armour)

In vanilla, an `Armour = 120` unit rolls between `60%` and `120%` reduction on non-AP damage `N`, and anything above `100%` is clamped away. By `Armour = 200`, non-AP damage is `100%` blocked on every roll, hitting a hard ceiling at `N / D` (`50%` total reduction against a `50%` AP attack).

With **Refined Armour Penetration**, armour is scaled by `(1 - p)` **first** to obtain `eff_armour`, and high effective armour smoothly enters the `Hyperbolic(S = 20)` tail (`eff / (eff + 20)`), so `120–200` armour continues to provide meaningful, smooth protection without ever becoming 100% invulnerable.

### Reduction Curve: `ratio = f(Armour)` at `50%` AP Share (`N = 50, A = 50`)

![Damage Reduction Ratio Curve at 50% AP](docs/armour_curve_50ap.svg)

| Target Armour | Vanilla Avg Reduction | `gamma = 1.0` | `gamma = 1.3, S = 20` (Default) | `gamma = 1.4, S = 20` |
|---:|---:|---:|---:|---:|
| **0** | `0.00%` | `0.00%` | `0.00%` | `0.00%` |
| **20** | `7.50%` | `7.50%` | `8.91%` | `9.32%` |
| **40** | `15.00%` | `15.00%` | `17.82%` | `18.63%` |
| **60** | `22.50%` | `22.50%` | `26.72%` | `27.95%` |
| **80** | `30.00%` | `30.00%` | `35.63%` | `37.26%` |
| **100** | `37.50%` | `37.50%` | `44.54%` | `46.58%` |
| **120** | `43.33%` *(overflows)* | `45.00%` | `53.45%` | `55.90%` |
| **140** | `46.79%` *(overflows)* | `52.50%` | `62.28%` | `64.85%` |
| **160** | `48.75%` *(overflows)* | `60.00%` | `69.64%` | `71.91%` |
| **180** | `49.72%` *(overflows)* | `66.76%` | `75.37%` | `77.34%` |
| **200** | `50.00%` *(hard wall)* | `72.22%` | `79.81%` | `81.49%` |

### How Native Injection Works (Open-Source Transparency)

- Because WARHAMMER III's armour formula is compiled in native C++ (`Warhammer3.exe`), Lua scripts alone cannot change it.
- This mod compiles a tiny (~61 KB) open-source Windows DLL (`refined_armour_penetration_v1.dll`) embedded inside the `.pack` as hex data (`refined_armour_penetration_bin.lua`).
- On startup, the Lua script checks if `refined_armour_penetration_v1.dll` exists in the game root with the exact expected byte size; if not, it writes it out once and loads it via `require`.
- The DLL uses **signature pattern scanning** (no hardcoded addresses) to hook the 3 armour roll call sites in memory when a battle begins, and cleanly restores them when exiting.
- **No external network calls, no registry edits, no background processes.** Full C source is in `src/refined_armour_penetration.c`.

### MCT Options & Compatibility

- **Mod Configuration Tool (MCT)**: Tune `enabled`, `armour_mode` (`Combined`, `Refined Linear Only`, `Hyperbolic Only`), `gamma`, `lower_cap`, `hyp_step` (`S`), `min_damage`, `use_kv_lower_cap`, and `diag_enabled` in-game without restarting. Works out-of-the-box without MCT using defaults.
- **Compatibility**: Compatible with almost all unit/overhaul mods (does not touch DB tables).
- **Platform**: **Windows x86_64 only**. Mac/Linux users will safely fall back to vanilla behaviour without crashing.
- **Multiplayer**: Both players must enable the mod with identical MCT settings to avoid desyncs.

---

## 中文说明

### 简介

在《全面战争：战锤 3》原版机制中，破甲伤害 `A` 完全无视护甲，而非破甲伤害 `N` 单独结算护甲减免（`N * (1 - red) + A`）。这带来了两个核心问题：
1. 破甲与非破甲两段伤害完全割裂，高破甲武器无法帮助同一次攻击中的非破甲部分穿透护甲。
2. 当目标护甲超过 `100` 时，非破甲部分会出现严重的**护甲溢出**（掷骰超过 `100%` 的部分直接被截断浪费），高护甲收益迅速递减并撞上死板上限。

**Refined Armour Penetration** 在底层战斗伤害结算的 3 个入口点，将原版两段式结算替换为**统一的比例破甲公式**，并在高护甲段平滑过渡到双曲收益曲线。

### 护甲算法公式

设单次攻击基础伤害为 `N`，破甲伤害为 `A`，总面板伤害 `D = N + A`，目标护甲为 `armour`：
1. **破甲占比**：`r = clamp(A / D, 0, 1)`
2. **穿透系数**：`p = r ^ gamma`（默认 `gamma = 1.30`）
3. **有效护甲**：`eff_armour = armour * (1 - p)`
4. **两条减伤曲线**：
   - **线性掷骰（无硬上限）**：`roll = uniform(eff_armour * lower_cap, eff_armour)`，`red_lin = clamp(roll * 0.01, 0, 1)`（默认 `lower_cap = 0.50`）
   - **双曲减伤**：`red_hyp = eff_armour / (eff_armour + S)`（默认 `S = 20`）
5. **模式选择（可在 MCT 切换）**：
   - **`Combined`（默认）**：`red = min(red_lin, red_hyp)`
   - **`Refined Linear Only`**：`red = red_lin`
   - **`Hyperbolic Only`**：`red = red_hyp`
6. **最终伤害**：`dmg = max(min_damage, D * (1 - red))`（默认保底 `min_damage = 1.0`）

### `gamma` 旋钮、结合模式与手感解析

- **为什么默认采用 `min(线性掷骰, 双曲(20))`？**
  相比一刀切的 `85%` 硬封顶，取 `min(red_lin, red_hyp)` 可以在中低有效护甲段（`0 ~ 100`）完整保留线性的“重甲门槛与加速质感”，而在高有效护甲段（`110+`）平滑切入 `Hyperbolic(S = 20)` 的渐近曲线。同时因为线性掷骰 `red_lin` 在 `[0.5 * eff, 1.0 * eff]` 之间随机波动，高骰部分会提前接触双曲线，使期望减伤曲线在分叉处实现**零折点的自然过渡**。
- **`gamma = 1.00`（未溢出区间伤害期望与原版完全相同，但单次分布不同）**：
  当 `gamma = 1.0` 且护甲未发生溢出（`armour <= 100`）时，`eff_armour = armour * (N / D)`，平均减伤率为 `0.75 * armour * 0.01 * (N / D)`。乘以总伤害 `D` 后，期望伤害为 `D - N * (0.75 * armour * 0.01) = N * (1 - 0.75 * armour * 0.01) + A`，**数学期望与原版分毫不差**。
  但两者的**单次伤害分布不同**：原版只对非破甲部分 `N` 掷骰子，破甲部分 `A` 零波动全额穿透；而新算法是用缩放后的有效护甲对总伤害 `D` 统一掷骰，单次波动按比例平摊到整体伤害上，输出曲线更平滑。
- **`gamma = 1.30`（默认推荐值——扎实的重甲手感）**：
  在 `1.0` 的基础上适度抬高了中高护甲对中低破甲攻击的抗性（例如 `50%` 破甲占比的穿透率由 `50%` 降至 `40.6%`）。重装步兵/重骑兵面对低破甲杂兵与通用武器时明显更耐打、“铁罐头”质感更足，而真正的高破甲专精单位（`70%+` 破甲）依然能高效破甲。
- **`gamma = 1.40+`（更硬核的护甲区分度）**：
  进一步拉开“纯破甲专精”与“半破甲/混合伤害”之间的差距（`50%` 破甲占比穿透率降至 `37.9%`）。

### 解决原版“护甲溢出”问题（`100+` 护甲）

在原版中，一旦护甲超过 `100`（例如 `120` 护甲掷骰区间为 `60% ~ 120%`），超出 `100%` 的部分会被强行截断为 `100%`；到了 `200` 护甲时非破甲伤害几乎每次都被 `100%` 格挡，导致高护甲面对混合伤害时很快触及收益死墙（例如面对 `50%` AP 攻击，原版最高只能减免 `50%` 总伤）。

本 Mod 先按 `(1 - p)` 折算为有效护甲 `eff_armour`，再在高甲区平滑接入 `Hyperbolic(S = 20)`：即使面对 `50%` 破甲攻击，`120 ~ 200` 护甲依然能平滑、有意义地提供递进防护，既彻底消除了 `100+` 护甲的溢出失真，又不会让重甲彻底免疫伤害。

### 50% AP 伤害占比下的减伤曲线对比：`ratio = f(armour)`

![50% AP 占比下原版与新算法减伤曲线对比](docs/armour_curve_50ap.svg)

| 目标护甲 | 原版平均减伤比 | `gamma = 1.0` | `gamma = 1.3, S = 20`（默认） | `gamma = 1.4, S = 20` |
|---:|---:|---:|---:|---:|
| **0** | `0.00%` | `0.00%` | `0.00%` | `0.00%` |
| **20** | `7.50%` | `7.50%` | `8.91%` | `9.32%` |
| **40** | `15.00%` | `15.00%` | `17.82%` | `18.63%` |
| **60** | `22.50%` | `22.50%` | `26.72%` | `27.95%` |
| **80** | `30.00%` | `30.00%` | `35.63%` | `37.26%` |
| **100** | `37.50%` | `37.50%` | `44.54%` | `46.58%` |
| **120** | `43.33%` *(开始溢出)* | `45.00%` | `53.45%` | `55.90%` |
| **140** | `46.79%` *(严重溢出)* | `52.50%` | `62.28%` | `64.85%` |
| **160** | `48.75%` *(严重溢出)* | `60.00%` | `69.64%` | `71.91%` |
| **180** | `49.72%` *(接近死墙)* | `66.76%` | `75.37%` | `77.34%` |
| **200** | `50.00%` *(封顶死墙)* | `72.22%` | `79.81%` | `81.49%` |

### 原生注入原理与安全性说明

- 《战锤 3》的护甲结算逻辑编译在 `Warhammer3.exe` 原生 C++ 代码中，纯 Lua 或 DB 表无法修改计算公式。
- 本 Mod 将开源编译的 64 位原生 DLL（`refined_armour_penetration_v1.dll`，约 61 KB）内嵌于 `.pack` 包的 `refined_armour_penetration_bin.lua` 中。
- 游戏启动时，Lua 脚本会校验游戏根目录下是否存在同大小 DLL；若不存在则自动释放一次并通过 `require` 加载（订阅即用，无需玩家手动复制 DLL）。
- DLL 内部使用**特征码扫描**定位 3 处护甲结算点，在进入战斗时启用，战斗结束自动停用，退出游戏自动还原。无任何联网、注册表或后台驻留行为，完整 C 源码全部公开在本仓库 `src/` 目录。

### MCT 调参与兼容性

- **支持 MCT（Mod Configuration Tool）**：可在游戏内随时切换 `armour_mode`（结合 / 纯线性 / 纯双曲）、调节 `gamma`、`lower_cap`、`hyp_step`（`S`）、`min_damage`、是否使用原版 `_kv_rules` 下限、以及是否开启 `5s` 诊断日志（`refined_armour_penetration_diag.txt`）。不装 MCT 也可按默认参数正常运行。
- **兼容性**：不修改任何 DB 表，兼容绝大部分兵种与大型 Mod（如 SFO、Radious 等）。
- **平台与联机**：仅支持 **Windows 64 位**（Mac/Linux 会安全回退为原版逻辑不会闪退）；联机时需双方同时启用且 MCT 设置一致，否则会不同步。
