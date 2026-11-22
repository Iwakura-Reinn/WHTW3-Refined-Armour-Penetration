-- refined_armour_penetration.lua (script/_lib/mod/refined_armour_penetration.lua)
-- Refined Armour Penetration (Two-Term Cushioned AP):
--   DMG(N, A, M) = N * (1 - red(M)) + A * (1 - red(c_ap * M))
--   red(m) = min(red_lin(m), red_hyp(m)) in Combined mode:
--     red_lin(m) = clamp(uniform(m * lower_cap, m) * 0.01, 0, 1)
--     red_hyp(m) = m / (m + hyp_step)
--   dmg = max(min_damage, DMG)

local VERSION = 1
local MODULE = "refined_armour_penetration_v1"
local DLL_FILE = MODULE .. ".dll"
local BIN_MODULE = "refined_armour_penetration_bin"

local SETTINGS = {
    enabled = true,
    ap_armour_factor = 0.25,
    lower_cap = 0.65,
    armour_mode = "combined", -- "combined" (min), "linear", "hyperbolic"
    hyp_step = 20,
    min_damage = 1.0,
    use_kv_lower_cap = false,
    diag_enabled = true,
    diag_interval_ms = 5000,
}

local MODE_MAP = {
    combined = 0,
    linear = 1,
    hyperbolic = 2,
}

local api = {
    version = VERSION,
    settings = SETTINGS,
    native = nil,
    status = "uninitialized",
    error = nil,
    is_battle_active = false,
}
_G.refined_armour_penetration = api

local function log(msg)
    if type(ModLog) == "function" then
        pcall(ModLog, "[RAP] " .. tostring(msg))
    end
end
api.log = log

local function apply_config(mod, enable_flag)
    if not mod or type(mod.configure) ~= "function" then
        return
    end
    local en = enable_flag and 1 or 0
    local use_kv = SETTINGS.use_kv_lower_cap and 1 or 0
    local mode_int = MODE_MAP[SETTINGS.armour_mode] or 0
    pcall(
        mod.configure,
        en,
        SETTINGS.ap_armour_factor,
        SETTINGS.lower_cap,
        mode_int,
        SETTINGS.hyp_step,
        SETTINGS.min_damage,
        use_kv
    )
end
api.apply_config = apply_config

local function ensure_dll_extracted()
    local ok_bin, bin = pcall(require, BIN_MODULE)
    if not ok_bin or type(bin) ~= "table" or type(bin.hex) ~= "string" or type(bin.size) ~= "number" then
        return false, "embedded binary module missing: " .. tostring(bin)
    end

    local tag_file = DLL_FILE .. ".tag"
    local f_check = io.open(DLL_FILE, "rb")
    if f_check then
        local sz = f_check:seek("end")
        f_check:close()
        local f_tag = io.open(tag_file, "rb")
        local cur_tag = f_tag and f_tag:read("*a") or nil
        if f_tag then f_tag:close() end
        if sz == bin.size and type(bin.tag) == "string" and cur_tag == bin.tag then
            return true, "up_to_date"
        end
    end

    local raw = bin.hex:gsub("..", function(cc)
        return string.char(tonumber(cc, 16))
    end)

    if #raw ~= bin.size then
        return false, "decoded size mismatch (" .. tostring(#raw) .. " vs " .. tostring(bin.size) .. ")"
    end

    local f_out, err = io.open(DLL_FILE, "wb")
    if not f_out then
        return false, "failed to write " .. DLL_FILE .. ": " .. tostring(err)
    end
    f_out:write(raw)
    f_out:close()

    if type(bin.tag) == "string" then
        local f_t = io.open(tag_file, "wb")
        if f_t then
            f_t:write(bin.tag)
            f_t:close()
        end
    end
    return true, "extracted"
end

local function load_native()
    local ok_ext, ext_msg = ensure_dll_extracted()
    if not ok_ext then
        log("extract note: " .. tostring(ext_msg))
    end

    local ok, mod = pcall(require, MODULE)
    if not ok then
        return nil, "require('" .. MODULE .. "') failed: " .. tostring(mod)
    end
    if type(mod) ~= "table" then
        return nil, "require('" .. MODULE .. "') returned " .. type(mod)
    end
    if mod.version ~= VERSION then
        return nil, "DLL version mismatch (expected " .. VERSION .. ", got " .. tostring(mod.version) .. "); restart game"
    end
    return mod
end

local function sync_from_mct(mct_mod)
    if not mct_mod then return end
    local function get_opt(key, default_val)
        local opt = mct_mod:get_option_by_key(key)
        if opt then
            local val = opt:get_finalized_setting()
            if val ~= nil then return val end
        end
        return default_val
    end

    SETTINGS.enabled = get_opt("enabled", SETTINGS.enabled)
    SETTINGS.ap_armour_factor = tonumber(get_opt("ap_armour_factor", SETTINGS.ap_armour_factor)) or SETTINGS.ap_armour_factor
    SETTINGS.lower_cap = tonumber(get_opt("lower_cap", SETTINGS.lower_cap)) or SETTINGS.lower_cap
    SETTINGS.armour_mode = tostring(get_opt("armour_mode", SETTINGS.armour_mode))
    SETTINGS.hyp_step = tonumber(get_opt("hyp_step", SETTINGS.hyp_step)) or SETTINGS.hyp_step
    SETTINGS.min_damage = tonumber(get_opt("min_damage", SETTINGS.min_damage)) or SETTINGS.min_damage
    SETTINGS.use_kv_lower_cap = get_opt("use_kv_lower_cap", SETTINGS.use_kv_lower_cap)
    SETTINGS.diag_enabled = get_opt("diag_enabled", SETTINGS.diag_enabled)

    if api.native then
        apply_config(api.native, api.is_battle_active and SETTINGS.enabled)
    end
end
api.sync_from_mct = sync_from_mct

if core and type(core.add_listener) == "function" then
    core:add_listener(
        "rap_mct_init",
        "MctInitialized",
        true,
        function(context)
            local mct = context:mct()
            if mct then
                local mct_mod = mct:get_mod_by_key("refined_armour_penetration")
                if mct_mod then
                    sync_from_mct(mct_mod)
                end
            end
        end,
        true
    )

    core:add_listener(
        "rap_mct_finalized",
        "MctOptionSettingFinalized",
        true,
        function(context)
            local mct_mod = context:mod()
            if mct_mod and mct_mod:get_key() == "refined_armour_penetration" then
                sync_from_mct(mct_mod)
            end
        end,
        true
    )
end

if core and type(core.is_battle) == "function" and not core:is_battle() then
    local ok, mod = pcall(require, MODULE)
    if ok and type(mod) == "table" and mod.version == VERSION then
        apply_config(mod, false)
        api.native = mod
        api.status = "standby (campaign/frontend)"
    else
        -- Pre-extract DLL on startup/campaign so battle load is instant
        ensure_dll_extracted()
    end
elseif core and type(core.is_battle) == "function" and core:is_battle() then
    local mod, why = load_native()
    if mod then
        local ok, info = mod.install()
        if ok then
            api.native = mod
            api.status = tostring(info)
            apply_config(mod, false)
            log("installed v" .. VERSION .. ": " .. api.status)
        else
            api.error = tostring(info)
            api.status = "install_failed"
            log("DISABLED: " .. api.error)
        end
    else
        api.error = why
        api.status = "load_failed"
        log("DISABLED: " .. api.error)
    end
end
