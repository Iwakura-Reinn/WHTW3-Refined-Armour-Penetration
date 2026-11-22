-- refined_armour_penetration_mct.lua (script/mct/settings/refined_armour_penetration_mct.lua)
-- Mod Configuration Tool (MCT) integration for Refined Armour Penetration.

if not get_mct then return end
local mct = get_mct()
if not mct then return end

local mct_mod = mct:register_mod("refined_armour_penetration")
mct_mod:set_title("Refined Armour Penetration (AP Overhaul)", false)
mct_mod:set_author("Iwakura-Reinn", false)
mct_mod:set_description("Replaces vanilla armour resolution with Two-Term Cushioned AP: DMG = N*(1 - red(M)) + A*(1 - red(c_ap*M)).", false)

local opt_enabled = mct_mod:add_new_option("enabled", "checkbox")
opt_enabled:set_text("Enable Mod")
opt_enabled:set_tooltip_text("Toggle the refined armour penetration formula on or off.")
opt_enabled:set_default_value(true)

local opt_cap = mct_mod:add_new_option("ap_armour_factor", "slider")
opt_cap:set_text("AP Armour Factor (c_ap)")
opt_cap:set_tooltip_text("Fraction of target armour applied to AP damage: A * (1 - red(c_ap * M)). 0.00 = vanilla-style AP immunity, 0.25 = default balanced cushion.")
opt_cap:slider_set_min_max(0.00, 0.60)
opt_cap:slider_set_step_size(0.05, 2)
opt_cap:slider_set_precision(2)
opt_cap:set_default_value(0.25)

local opt_lower = mct_mod:add_new_option("lower_cap", "slider")
opt_lower:set_text("Armour Roll Lower Bound")
opt_lower:set_tooltip_text("Minimum fraction of effective armour used in each random roll (0.65 = uniform [0.65*eff, 1.0*eff]).")
opt_lower:slider_set_min_max(0.00, 1.00)
opt_lower:slider_set_step_size(0.05, 2)
opt_lower:slider_set_precision(2)
opt_lower:set_default_value(0.65)

local opt_mode = mct_mod:add_new_option("armour_mode", "dropdown")
opt_mode:set_text("Armour Reduction Mode", false)
opt_mode:set_tooltip_text("Combined: min(Refined Linear, Hyperbolic) [Default]. Linear: uncapped random roll. Hyperbolic: deterministic eff / (eff + S).", false)
opt_mode:add_dropdown_values({
    { key = "combined",   text = "Combined: min(Linear, Hyperbolic)", tt = "Uses Refined Linear roll at low/mid armour, smoothly transitioning to Hyperbolic at high armour.", is_default = true },
    { key = "linear",     text = "Refined Linear Only",               tt = "Uses uncapped Refined Linear random roll only.", is_default = false },
    { key = "hyperbolic", text = "Hyperbolic Only",                   tt = "Uses deterministic eff_armour / (eff_armour + S) only.", is_default = false },
})

local opt_step = mct_mod:add_new_option("hyp_step", "slider")
opt_step:set_text("Hyperbolic Step (S)", false)
opt_step:set_tooltip_text("Armour step S for Hyperbolic / Combined mode: red = eff / (eff + S). Default: 20.", false)
opt_step:slider_set_min_max(5, 150)
opt_step:slider_set_step_size(5, 0)
opt_step:slider_set_precision(0)
opt_step:set_default_value(20)

local opt_mindmg = mct_mod:add_new_option("min_damage", "slider")
opt_mindmg:set_text("Minimum Damage Floor")
opt_mindmg:set_tooltip_text("Minimum damage dealt per hit after armour reduction.")
opt_mindmg:slider_set_min_max(0.0, 10.0)
opt_mindmg:slider_set_step_size(0.5, 1)
opt_mindmg:slider_set_precision(1)
opt_mindmg:set_default_value(1.0)

local opt_kv = mct_mod:add_new_option("use_kv_lower_cap", "checkbox")
opt_kv:set_text("Use Vanilla _kv_rules Lower Cap")
opt_kv:set_tooltip_text("If checked, uses the vanilla _kv_rules armour_roll_lower_cap instead of the slider above.")
opt_kv:set_default_value(false)

local opt_diag = mct_mod:add_new_option("diag_enabled", "checkbox")
opt_diag:set_text("Enable Diagnostic Log")
opt_diag:set_tooltip_text("Writes refined_armour_penetration_diag.txt in the game root every 5s during battle.")
opt_diag:set_default_value(true)
