# Refined Armour Penetration (AP Overhaul)

> **Total War: WARHAMMER III** native combat formula overhaul — **Windows (x86_64) Only**  
> Supports **Mod Configuration Tool (MCT)** & **One-Click Steam Workshop Subscribe** (auto-extracts native DLL from `.pack`).

---

## English

### Overview

In vanilla WARHAMMER III, Armour-Piercing (AP) damage `A` completely ignores armour, while non-AP damage `N` is reduced independently (`N * (1 - red) + A`). This leads to two well-known issues:
1. Low-AP elite units (e.g. heavy cavalry or non-AP infantry) struggle disproportionately against high-armour targets.
2. High-AP weapons do not help the non-AP portion of the same strike pierce armour at all.

**Refined Armour Penetration** replaces the vanilla two-part armour resolution at all three battle damage sites (melee/collision struct, and both integer missile/direct damage sites) with a **unified proportional penetration model**.

### Armour Algorithm

Let:
- `N` = Base (non-AP) damage
- `A` = Armour-Piercing (AP) damage
- `D = N + A` = Total raw damage
- `armour` = Target's armour value

The new formula computes:
1. **AP Ratio**: `r = clamp(A / D, 0, 1)`
2. **Penetration Factor**: `p = r ^ gamma` (default `gamma = 1.30`)
3. **Effective Armour**: `eff_armour = armour * (1 - p)`
4. **Random Armour Roll**: `roll = uniform(eff_armour * lower_cap, eff_armour)` (default `lower_cap = 0.50`, average `0.75 * eff_armour`)
5. **Damage Reduction**: `red = clamp(roll * 0.01, 0, max_reduction)` (default `max_reduction = 0.85`)
6. **Final Damage**: `dmg = max(min_damage, D * (1 - red))` (default `min_damage = 1.0`)

#### Vanilla vs. Refined Comparison (`D = 100`, `Armour = 100`, `gamma = 1.30`, `lower_cap = 0.50`)

Using expected average roll (`0.75 * eff_armour`):

| AP Share (`A / D`) | Raw `N + A` | Vanilla Avg Damage | Penetration `p = r^1.3` | Effective Armour | Refined Avg Damage | Difference |
|---:|---:|---:|---:|---:|---:|---:|
| **10%** | `90 + 10` | `32.5` | `5.0%` | `95.0` | `28.8` | `-3.7` (heavy armour holds firm vs low-AP) |
| **30%** | `70 + 30` | `47.5` | `20.9%` | `79.1` | `40.7` | `-6.8` |
| **50%** | `50 + 50` | `62.5` | `40.6%` | `59.4` | `55.5` | `-7.0` |
| **70%** | `30 + 70` | `77.5` | `62.9%` | `37.1` | `72.2` | `-5.3` |
| **90%** | `10 + 90` | `92.5` | `87.2%` | `12.8` | `90.4` | `-2.1` |

*Note: Against **120+ Armour**, vanilla caps `N` reduction at 100% (`N * 0`), whereas Refined caps total reduction at `85%` (`max_reduction = 0.85`) and scales `eff_armour` smoothly, making `gamma` and `max_reduction` clean, intuitive levers in MCT.*

### How Native Injection Works (Open-Source Transparency)

- Because WARHAMMER III's armour formula is compiled in native C++ (`Warhammer3.exe`), Lua scripts alone cannot change it.
- This mod compiles a tiny (~58 KB) open-source Windows DLL (`refined_armour_penetration_v1.dll`) embedded inside the `.pack` as hex data (`refined_armour_penetration_bin.lua`).
- On startup, the Lua script checks if `refined_armour_penetration_v1.dll` exists in the game root with the exact expected byte size; if not, it writes it out once and loads it via `require`.
- The DLL uses **signature pattern scanning** (no hardcoded addresses) to hook the 3 armour roll call sites in memory when a battle begins, and cleanly restores them when exiting.
- **No external network calls, no registry edits, no background processes.** Full C source is in `src/refined_armour_penetration.c`.

### MCT Options & Compatibility

- **Mod Configuration Tool (MCT)**: All parameters (`enabled`, `gamma`, `lower_cap`, `max_reduction`, `min_damage`, `use_kv_lower_cap`, `diag_enabled`) can be tuned in-game via MCT without restarting. Works out-of-the-box with sensible defaults even without MCT.
- **Compatibility**: Compatible with almost all unit/overhaul mods (does not touch DB tables).
- **Platform**: **Windows x86_64 only**. Mac/Linux users will safely fall back to vanilla behaviour without crashing.
- **Multiplayer**: Both players must enable the mod with identical MCT settings to avoid desyncs.

---

## 中文说明

### 简介

在《全面战争：战锤 3》原版机制中，破甲伤害 `A` 完全无视护甲，而非破甲伤害 `N` 单独结算护甲减免（`N * (1 - red) + A`）。这导致高护甲目标面对非破甲/低破甲攻击时两段伤害完全割裂，且原版在 100+ 护甲时无法通过参数平滑控制最大减伤上限。

**Refined Armour Penetration** 在底层战斗伤害结算的 3 个入口点，将原版两段式结算替换为**统一的比例破甲公式**。

### 护甲算法公式

设单次攻击基础伤害为 `N`，破甲伤害为 `A`，总面板伤害 `D = N + A`，目标护甲为 `armour`：
1. **破甲占比**：`r = clamp(A / D, 0, 1)`
2. **穿透系数**：`p = r ^ gamma`（默认 `gamma = 1.30`）
3. **有效护甲**：`eff_armour = armour * (1 - p)`
4. **护甲随机掷骰**：`roll = uniform(eff_armour * lower_cap, eff_armour)`（默认 `lower_cap = 0.50`，均值 `0.75 * eff_armour`）
5. **最终减伤率**：`red = clamp(roll * 0.01, 0, max_reduction)`（默认最高减伤 `max_reduction = 0.85`）
6. **最终伤害**：`dmg = max(min_damage, D * (1 - red))`（默认保底 `min_damage = 1.0`）

#### 原版 vs 新算法数值对比（`总伤害 D = 100`，`护甲 = 100`，`gamma = 1.30`，`lower_cap = 0.50`）

按平均掷骰（`0.75 * 有效护甲`）计算：

| 破甲占比 (`A / D`) | 面板 (`N + A`) | 原版期望伤害 | 穿透率 `p = r^1.3` | 有效护甲 | 新算法期望伤害 | 对比说明 |
|---:|---:|---:|---:|---:|---:|---:|
| **10%** | `90 + 10` | `32.5` | `5.0%` | `95.0` | `28.8` | 低破甲武器面对重甲更难打穿 |
| **30%** | `70 + 30` | `47.5` | `20.9%` | `79.1` | `40.7` | 重甲对中低破甲依然保持高防御 |
| **50%** | `50 + 50` | `62.5` | `40.6%` | `59.4` | `55.5` | 混合伤害平滑过渡 |
| **70%** | `30 + 70` | `77.5` | `62.9%` | `37.1` | `72.2` | 高破甲显著削减目标有效护甲 |
| **90%** | `10 + 90` | `92.5` | `87.2%` | `12.8` | `90.4` | 接近纯破甲武器无视绝大部分护甲 |

*可通过 MCT 调低 `gamma`（如 `1.00` 或 `0.85`）让破甲占比更强势，或调整 `max_reduction` 控制重甲免伤上限。*

### 原生注入原理与安全性说明

- 《战锤 3》的护甲结算逻辑编译在 `Warhammer3.exe` 原生 C++ 代码中，纯 Lua 或 DB 表无法修改计算公式。
- 本 Mod 将开源编译的 64 位原生 DLL（`refined_armour_penetration_v1.dll`，约 58 KB）内嵌于 `.pack` 包的 `refined_armour_penetration_bin.lua` 中。
- 游戏启动时，Lua 脚本会校验游戏根目录下是否存在同大小 DLL；若不存在则自动释放一次并通过 `require` 加载（订阅即用，无需玩家手动复制 DLL）。
- DLL 内部使用**特征码扫描**定位 3 处护甲结算点，在进入战斗时启用，战斗结束自动停用，退出游戏自动还原。无任何联网、注册表或后台驻留行为，完整 C 源码全部公开在本仓库 `src/` 目录。

### MCT 调参与兼容性

- **支持 MCT（Mod Configuration Tool）**：可在游戏内随时调节 `gamma`、`lower_cap`、`max_reduction`、`min_damage`、是否使用原版 `_kv_rules` 下限、以及是否开启 `5s` 诊断日志（`refined_armour_penetration_diag.txt`）。不装 MCT 也可按默认参数正常运行。
- **兼容性**：不修改任何 DB 表，兼容绝大部分兵种与大型 Mod（如 SFO、Radious 等）。
- **平台与联机**：仅支持 **Windows 64 位**（Mac/Linux 会安全回退为原版逻辑不会闪退）；联机时需双方同时启用且 MCT 设置一致，否则会不同步。
