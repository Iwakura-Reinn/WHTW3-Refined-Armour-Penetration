# Refined Armour Penetration (AP Overhaul)

> **Total War: WARHAMMER III** native combat formula overhaul — **Windows (x86_64) Only**  
> Supports **Mod Configuration Tool (MCT)** & **One-Click Steam Workshop Subscribe** (auto-extracts native DLL from `.pack`).

---

## English

### Overview

In vanilla WARHAMMER III, Armour-Piercing (AP) damage `A` completely ignores armour (`0%` reduction regardless of how heavy the target is), while non-AP damage `N` is reduced by a linear roll clamped at `100%`:

- **Vanilla**: `DMG = N * (1 - clamp(roll(0.5 * M, M) * 0.01, 0, 1)) + A`

This causes two long-standing issues:
1. **High-armour overflow & hard wall (`100+` Armour)**: Once `roll` exceeds `100%`, extra armour is clipped away. Against a `50%` AP attack, `200` Armour hits a hard ceiling of `50%` total reduction.
2. **All-or-nothing AP immunity**: Even `180–200` Armour units take full `100%` AP damage with zero mitigation, making heavy armour feel brittle against AP-heavy armies.

**Refined Armour Penetration** replaces the vanilla armour resolution at all three battle damage sites (melee/collision struct, and both integer missile/direct damage sites) with **Two-Term Cushioned AP** and a smooth high-armour tail.

*(Design note: earlier prototypes explored a single-term proportional AP-ratio formula `p = (A / D)^gamma`, which was later replaced by the cleaner, strictly monotonic Two-Term Cushioned AP model below.)*

### Armour Algorithm: Two-Term Cushioned AP

Let:
- `N` = Base (non-AP) damage
- `A` = Armour-Piercing (AP) damage
- `M` = Target's armour value

For any effective armour `m`, define the reduction curve `red(m)`:
1. **Refined Linear Roll (Uncapped)**: `roll(m) = uniform(m * lower_cap, m)`, `red_lin(m) = clamp(roll(m) * 0.01, 0, 1)` *(default `lower_cap = 0.65`)*
2. **Hyperbolic Tail**: `red_hyp(m) = m / (m + S)` *(default `S = 20`)*
3. **Mode Selection (configurable in MCT)**:
   - **`Combined` (Default)**: `red(m) = min(red_lin(m), red_hyp(m))`
   - **`Refined Linear Only`**: `red(m) = red_lin(m)`
   - **`Hyperbolic Only`**: `red(m) = red_hyp(m)`

Final damage is computed as:
- **`DMG(N, A, M) = max(min_damage, N * (1 - red(M)) + A * (1 - red(c_ap * M)))`** *(default `c_ap = 0.25`, `min_damage = 1.0`)*

### Key Differences vs. Vanilla

| Aspect | Vanilla | Refined Armour Penetration (Two-Term Cushioned AP) |
|---|---|---|
| **Base Damage `N`** | `roll(0.5 * M, M)`, clamped at `100%` | `min(Linear(0.65 * M, M), M / (M + 20))` — stronger mid-armour floor (`0.65` vs `0.50`), smooth high-armour tail without `100+` overflow |
| **AP Damage `A`** | `100%` ignores armour (`c_ap = 0`) | Cushioned by `c_ap * M` (default `c_ap = 0.25`), so heavy armour meaningfully mitigates AP without invalidating it |
| **Monotonicity** | Can plateau at `100+` Armour | Strictly increases EHP with Armour (`dEHP/dM > 0`), and strictly increases damage when adding either `N` or `A` (`dDMG/dN > 0`, `dDMG/dA > 0`) |
| **High-Armour Feel** | `120–200` Armour wastes rolls above `100%` | `120–200` Armour smoothly scales both `N` and `A` mitigation without ever reaching `100%` invulnerability |

### Reduction Curve Comparison & Interactive Explorer

> **[Open Interactive 2D Armour & AP Curve Explorer (GitHub Pages)](https://iwakura-reinn.github.io/WHTW3-Refined-Armour-Penetration/)**  
> Tune `c_ap`, `lower_cap`, and `Hyperbolic Step S` in real time, switch between EHP / Reduction % / Marginal Derivative, or inspect any `(Armour, AP%)` slice (`docs/index.html`).

| Target Armour | Vanilla Avg (`50%` AP) | Two-Term `c_ap = 0.25` (`50%` AP) | Two-Term `c_ap = 0.25` (`100%` AP) |
|---:|---:|---:|---:|
| **0** | `0.00%` | `0.00%` | `0.00%` |
| **20** | `7.50%` | `10.31%` | `4.12%` |
| **40** | `15.00%` | `20.62%` | `8.25%` |
| **60** | `22.50%` | `30.94%` | `12.38%` |
| **80** | `30.00%` | `41.25%` | `16.50%` |
| **100** | `37.50%` | `49.58%` | `20.62%` |
| **120** | `43.33%` *(overflows)* | `54.88%` | `24.75%` |
| **140** | `46.79%` *(overflows)* | `58.19%` | `28.88%` |
| **160** | `48.75%` *(overflows)* | `60.94%` | `33.00%` |
| **180** | `49.72%` *(overflows)* | `63.56%` | `37.12%` |
| **200** | `50.00%` *(hard wall)* | `66.08%` | `41.25%` |

*(Default parameters: `c_ap = 0.25`, `lower_cap = 0.65`, `S = 20`, `Combined` mode.)*

### How Native Injection Works (Open-Source Transparency)

- Because WARHAMMER III's armour formula is compiled in native C++ (`Warhammer3.exe`), Lua scripts alone cannot change it.
- This mod compiles a tiny (~56 KB) open-source Windows DLL (`refined_armour_penetration_v1.dll`) embedded inside the `.pack` as hex data (`refined_armour_penetration_bin.lua`).
- On startup, the Lua script checks if `refined_armour_penetration_v1.dll` exists in the game root with the exact expected size and SHA-256 tag; if not, it writes it out once and loads it via `require`.
- The DLL uses **signature pattern scanning** (no hardcoded addresses) to hook the 3 armour roll call sites in memory when a battle begins, and cleanly restores them when exiting.
- **No external network calls, no registry edits, no background processes.** Full C source is in `src/refined_armour_penetration.c`.

### MCT Options & Compatibility

- **Mod Configuration Tool (MCT)**: Tune `enabled`, `armour_mode` (`Combined`, `Refined Linear Only`, `Hyperbolic Only`), `ap_armour_factor` (`c_ap`), `lower_cap`, `hyp_step` (`S`), `min_damage`, `use_kv_lower_cap`, and `diag_enabled` in-game without restarting. Works out-of-the-box without MCT using defaults.
- **Compatibility**: Compatible with almost all unit/overhaul mods (does not touch DB tables).
- **Platform**: **Windows x86_64 only**. Mac/Linux users will safely fall back to vanilla behaviour without crashing.
- **Multiplayer**: Both players must enable the mod with identical MCT settings to avoid desyncs.

---

## 中文说明

### 简介

在《全面战争：战锤 3》原版机制中，破甲伤害 `A` 完全无视护甲（无论目标护甲多高都是 `0%` 减免），而非破甲伤害 `N` 单独按 `[0.5 * M, M]` 掷骰并在 `100%` 处硬截断：

- **原版公式**：`DMG = N * (1 - clamp(roll(0.5 * M, M) * 0.01, 0, 1)) + A`

这带来了两个长期存在的问题：
1. **高甲溢出与死墙（`100+` 护甲）**：护甲超过 `100` 后，超出 `100%` 的掷骰区间被直接截断浪费；面对 `50%` AP 攻击时，即使堆到 `200` 护甲也只能卡死在 `50%` 总减伤上限。
2. **破甲部分完全免疫护甲**：哪怕面对 `180–200` 护甲的重装单位，AP 伤害依然是 `0%` 减免，导致高甲单位在后期高破甲环境下显得非常脆弱。

**Refined Armour Penetration** 在底层战斗伤害结算的 3 个入口点，将原版护甲结算替换为**两段独立缓冲公式（Two-Term Cushioned AP）**，并引入平滑的高护甲双曲过渡。

*（附注：此前我们曾探索过按 `p = (A / D)^gamma` 统一折算有效护甲的方案；最终替换为下方更直观、在所有伤害与护甲区间均严格单调的两段缓冲模型。）*

### 护甲算法：两段独立缓冲 AP（Two-Term Cushioned AP）

设单次攻击基础伤害为 `N`，破甲伤害为 `A`，目标护甲为 `M`。对于任意有效护甲 `m`，定义减伤函数 `red(m)`：
1. **线性掷骰（无硬截断）**：`roll(m) = uniform(m * lower_cap, m)`，`red_lin(m) = clamp(roll(m) * 0.01, 0, 1)` *（默认 `lower_cap = 0.65`）*
2. **双曲渐近曲线**：`red_hyp(m) = m / (m + S)` *（默认 `S = 20`）*
3. **模式选择（可在 MCT 切换）**：
   - **`Combined`（默认）**：`red(m) = min(red_lin(m), red_hyp(m))`
   - **`Refined Linear Only`**：`red(m) = red_lin(m)`
   - **`Hyperbolic Only`**：`red(m) = red_hyp(m)`

最终伤害公式为：
- **`DMG(N, A, M) = max(min_damage, N * (1 - red(M)) + A * (1 - red(c_ap * M)))`** *（默认 `c_ap = 0.25`，`min_damage = 1.0`）*

### 与原版的核心区别

| 维度 | 原版机制 | 本 Mod（Two-Term Cushioned AP） |
|---|---|---|
| **非破甲伤害 `N`** | 按 `[0.5 * M, M]` 掷骰，`100%` 硬截断 | `min(Linear(0.65 * M, M), M / (M + 20))`：下限提升至 `0.65` 让重甲更稳，高甲区平滑接入双曲尾部，消除 `100+` 溢出 |
| **破甲伤害 `A`** | 完全无视护甲（等价于 `c_ap = 0`） | 按 `c_ap * M` 缓冲减免（默认 `c_ap = 0.25`）：重甲能有效减缓纯破甲伤害，同时高破甲依然保有绝对穿透优势 |
| **单调性保证** | `100+` 护甲对混合/高 AP 收益很快撞墙 | 护甲越高 EHP 严格越高（`dEHP/dM > 0`），且无论是增加 `N` 还是 `A` 都严格提升最终伤害，不存在任何数值倒挂 |
| **高甲手感** | `120–200` 护甲大量点数被截断浪费 | `120–200` 护甲对 `N` 和 `A` 均提供平滑递进的真实防护，且永远不会出现 `100%` 免伤 |

### 减伤曲线对比与在线交互工具

> **[点击打开在线交互式 2D 护甲与破甲曲线工具 (GitHub Pages)](https://iwakura-reinn.github.io/WHTW3-Refined-Armour-Penetration/)**  
> 支持实时拖动 `c_ap`、`lower_cap`、`Hyperbolic Step S`，自由切换 EHP / 减伤比例 / 边际导数纵轴及任意 `AP%` / `护甲` 切片（源码位于 `docs/index.html`）。

| 目标护甲 | 原版平均减伤（`50%` AP） | Two-Term `c_ap = 0.25`（`50%` AP） | Two-Term `c_ap = 0.25`（`100%` AP） |
|---:|---:|---:|---:|
| **0** | `0.00%` | `0.00%` | `0.00%` |
| **20** | `7.50%` | `10.31%` | `4.12%` |
| **40** | `15.00%` | `20.62%` | `8.25%` |
| **60** | `22.50%` | `30.94%` | `12.38%` |
| **80** | `30.00%` | `41.25%` | `16.50%` |
| **100** | `37.50%` | `49.58%` | `20.62%` |
| **120** | `43.33%` *(开始溢出)* | `54.88%` | `24.75%` |
| **140** | `46.79%` *(严重溢出)* | `58.19%` | `28.88%` |
| **160** | `48.75%` *(严重溢出)* | `60.94%` | `33.00%` |
| **180** | `49.72%` *(接近死墙)* | `63.56%` | `37.12%` |
| **200** | `50.00%` *(封顶死墙)* | `66.08%` | `41.25%` |

*（默认参数：`c_ap = 0.25`，`lower_cap = 0.65`，`S = 20`，`Combined` 模式。）*

### 原生注入原理与安全性说明

- 《战锤 3》的护甲结算逻辑编译在 `Warhammer3.exe` 原生 C++ 代码中，纯 Lua 或 DB 表无法修改计算公式。
- 本 Mod 将开源编译的 64 位原生 DLL（`refined_armour_penetration_v1.dll`，约 56 KB）内嵌于 `.pack` 包的 `refined_armour_penetration_bin.lua` 中。
- 游戏启动时，Lua 脚本会校验游戏根目录下是否存在同大小及同 SHA-256 校验标记的 DLL；若不一致则自动释放并通过 `require` 加载（订阅即用，无需玩家手动复制 DLL）。
- DLL 内部使用**特征码扫描**定位 3 处护甲结算点，在进入战斗时启用，战斗结束自动停用，退出游戏自动还原。无任何联网、注册表或后台驻留行为，完整 C 源码全部公开在本仓库 `src/` 目录。

### MCT 调参与兼容性

- **支持 MCT（Mod Configuration Tool）**：可在游戏内随时切换 `armour_mode`（结合 / 纯线性 / 纯双曲）、调节 `ap_armour_factor`（`c_ap`）、`lower_cap`、`hyp_step`（`S`）、`min_damage`、是否使用原版 `_kv_rules` 下限、以及是否开启 `5s` 诊断日志（`refined_armour_penetration_diag.txt`）。不装 MCT 也可按默认参数正常运行。
- **兼容性**：不修改任何 DB 表，兼容绝大部分兵种与大型 Mod（如 SFO、Radious 等）。
- **平台与联机**：仅支持 **Windows 64 位**（Mac/Linux 会安全回退为原版逻辑不会闪退）；联机时需双方同时启用且 MCT 设置一致，否则会不同步。
