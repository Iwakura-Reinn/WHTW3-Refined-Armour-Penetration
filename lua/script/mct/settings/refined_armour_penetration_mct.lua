-- refined_armour_penetration_mct.lua (script/mct/settings/refined_armour_penetration_mct.lua)
-- Mod Configuration Tool (MCT) integration for Refined Armour Penetration.

if not mct then return end

local mct_mod = mct:register_mod("refined_armour_penetration")
mct_mod:set_title("Refined Armour Penetration (AP Overhaul)")
mct_mod:set_author("Iwakura-Reinn")
mct_mod:set_description("Replaces vanilla two-part armour resolution with proportional AP penetration: p = (A / (N + A)) ^ gamma.")

local opt_enabled = mct_mod:add_new_option("enabled", "checkbox")
opt_enabled:set_text("Enable Mod")
opt_enabled:set_tooltip_text("Toggle the refined armour penetration formula on or off.")
opt_enabled:set_default_value(true)

local opt_gamma = mct_mod:add_new_option("gamma", "slider")
opt_gamma:set_text("Penetration Exponent (gamma)")
opt_gamma:set_tooltip_text("Controls how sharply AP ratio reduces effective armour: p = (A / (N + A)) ^ gamma. Lower = stronger AP penetration.")
opt_gamma:slider_set_min_max(0.50, 3.00)
opt_gamma:slider_set_step_size(0.05, 2)
opt_gamma:set_default_value(1.30)

local opt_lower = mct_mod:add_new_option("lower_cap", "slider")
opt_lower:set_text("Armour Roll Lower Bound")
opt_lower:set_tooltip_text("Minimum fraction of effective armour used in each random roll (0.50 = uniform [0.5*eff, 1.0*eff]).")
opt_lower:slider_set_min_max(0.00, 1.00)
opt_lower:slider_set_step_size(0.05, 2)
opt_lower:set_default_value(0.50)

local opt_maxred = mct_mod:add_new_option("max_reduction", "slider")
opt_maxred:set_text("Max Damage Reduction Cap")
opt_maxred:set_tooltip_text("Maximum damage reduction percentage from armour (0.85 = 85% cap).")
opt_maxred:slider_set_min_max(0.50, 1.00)
opt_maxred:slider_set_step_size(0.05, 2)
opt_maxred:set_default_value(0.85)

local opt_mindmg = mct_mod:add_new_option("min_damage", "slider")
opt_mindmg:set_text("Minimum Damage Floor")
opt_mindmg:set_tooltip_text("Minimum damage dealt per hit after armour reduction.")
opt_mindmg:slider_set_min_max(0.0, 10.0)
opt_mindmg:slider_set_step_size(0.5, 1)
opt_mindmg:set_default_value(1.0)

local opt_kv = mct_mod:add_new_option("use_kv_lower_cap", "checkbox")
opt_kv:set_text("Use Vanilla _kv_rules Lower Cap")
opt_kv:set_tooltip_text("If checked, uses the vanilla _kv_rules armour_roll_lower_cap instead of the slider above.")
opt_kv:set_default_value(false)

local opt_diag = mct_mod:add_new_option("diag_enabled", "checkbox")
opt_diag:set_text("Enable Diagnostic Log")
opt_diag:set_tooltip_text("Writes refined_armour_penetration_diag.txt in the game root every 5s during battle.")
opt_diag:set_default_value(true)
