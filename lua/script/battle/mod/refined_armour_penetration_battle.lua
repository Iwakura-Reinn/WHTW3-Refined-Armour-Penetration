-- refined_armour_penetration_battle.lua (script/battle/mod/refined_armour_penetration_battle.lua)
-- Activates Refined Armour Penetration during battles and writes optional diagnostics.

local api = rawget(_G, "refined_armour_penetration")
local DIAG_FILE = "refined_armour_penetration_diag.txt"
local TIMER_NAME = "refined_armour_penetration_diag_tick"

local function write_diag(reason)
    if not (api and api.settings and api.settings.diag_enabled) then
        return
    end
    if not (api.native and api.native.diag_text) then
        return
    end
    local ok, txt = pcall(api.native.diag_text)
    if not ok or not txt then
        return
    end
    local f = io.open(DIAG_FILE, "w")
    if f then
        local okt, ms = pcall(function() return bm:time_elapsed_ms() end)
        f:write("reason=", tostring(reason), " battle_ms=", tostring(okt and ms or "?"), "\n")
        f:write(txt)
        f:close()
    end
end

if api and api.native then
    api.is_battle_active = true
    pcall(api.native.reset_stats)
    api.apply_config(api.native, api.settings and api.settings.enabled)
    api.log("battle active: " .. tostring(api.status))
    write_diag("battle_init")

    if bm then
        if api.settings and api.settings.diag_enabled and api.settings.diag_interval_ms and api.settings.diag_interval_ms > 0 then
            bm:repeat_callback(function()
                write_diag("tick")
            end, api.settings.diag_interval_ms, TIMER_NAME)
        end

        bm:register_phase_change_callback("Complete", function()
            bm:remove_process(TIMER_NAME)
            write_diag("battle_complete")
            api.is_battle_active = false
            api.apply_config(api.native, false)
        end)
    end
elseif api and api.error and api.settings and api.settings.diag_enabled then
    local f = io.open(DIAG_FILE, "w")
    if f then
        f:write("DISABLED: " .. tostring(api.error) .. "\n")
        f:close()
    end
end
