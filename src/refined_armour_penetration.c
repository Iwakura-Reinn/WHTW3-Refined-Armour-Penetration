/*
 * refined_armour_penetration.c - Total War: WARHAMMER III Refined Armour Penetration (AP Overhaul)
 *
 * Replaces vanilla two-part (base + AP) armour resolution at all 3 battle damage sites:
 *   D = N + A
 *   r = clamp(A / D, 0, 1)
 *   p = r ^ gamma
 *   eff_armour = armour * (1 - p)
 *   roll = uniform(eff_armour * lower_cap, eff_armour)
 *   red  = clamp(roll * 0.01, 0, max_reduction)
 *   dmg  = max(min_damage, D * (1 - red))
 *
 * Build (MinGW-w64 x86_64):
 *   gcc -shared -O2 -Wall -Wextra -o refined_armour_penetration_v1.dll refined_armour_penetration.c -lm
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RAP_VERSION 1

/* ---------- Minimal Lua 5.1 C API (resolved dynamically from Warhammer3.exe) ---------- */

typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *L);
typedef float lua_Number;
typedef ptrdiff_t lua_Integer;

typedef struct {
    void (*createtable)(lua_State *L, int narr, int nrec);
    void (*setfield)(lua_State *L, int idx, const char *k);
    void (*pushcclosure)(lua_State *L, lua_CFunction fn, int n);
    void (*pushinteger)(lua_State *L, lua_Integer n);
    void (*pushnumber)(lua_State *L, lua_Number n);
    void (*pushboolean)(lua_State *L, int b);
    void (*pushstring)(lua_State *L, const char *s);
    void (*pushnil)(lua_State *L);
    lua_Integer (*tointeger)(lua_State *L, int idx);
    lua_Number (*tonumber)(lua_State *L, int idx);
    int (*toboolean)(lua_State *L, int idx);
    int (*gettop)(lua_State *L);
    int (*type)(lua_State *L, int idx);
} LuaAPI;

static LuaAPI g_lua;

static int resolve_lua_api(void) {
    HMODULE exe = GetModuleHandleA(NULL);
    if (!exe) return 0;

#define RESOLVE(field, name) do { \
    FARPROC p = GetProcAddress(exe, name); \
    if (!p) return 0; \
    memcpy(&g_lua.field, &p, sizeof(p)); \
} while (0)

    RESOLVE(createtable,  "lua_createtable");
    RESOLVE(setfield,     "lua_setfield");
    RESOLVE(pushcclosure, "lua_pushcclosure");
    RESOLVE(pushinteger,  "lua_pushinteger");
    RESOLVE(pushnumber,   "lua_pushnumber");
    RESOLVE(pushboolean,  "lua_pushboolean");
    RESOLVE(pushstring,   "lua_pushstring");
    RESOLVE(pushnil,      "lua_pushnil");
    RESOLVE(tointeger,    "lua_tointeger");
    RESOLVE(tonumber,     "lua_tonumber");
    RESOLVE(toboolean,    "lua_toboolean");
    RESOLVE(gettop,       "lua_gettop");
    RESOLVE(type,         "lua_type");
#undef RESOLVE
    return 1;
}

/* ---------- Runtime configuration & diagnostics ---------- */

typedef struct {
    volatile LONG enabled;
    float gamma;
    float lower_cap;
    int   mode;       /* 0 = min(linear, hyperbolic), 1 = linear only, 2 = hyperbolic only */
    float hyp_step;   /* hyperbolic step S (default 20.0) */
    float min_damage;
    int   use_kv_lower_cap;
} ArmourConfig;

static ArmourConfig g_cfg = {
    0,      /* enabled: off until battle script enables it */
    1.3f,   /* gamma */
    0.65f,  /* lower_cap */
    0,      /* mode: 0 = combined min(linear, hyperbolic) */
    20.0f,  /* hyp_step */
    1.0f,   /* min_damage */
    0       /* use_kv_lower_cap */
};

typedef struct {
    volatile LONG64 calls[3];
    volatile LONG64 active_calls[3];
    double sum_vanilla_dmg;
    double sum_new_dmg;
    double sum_pen_ratio;
    double sum_eff_armour;
    double sum_raw_armour;
    LONG64 sample_count;
} ArmourStats;

typedef struct {
    int   site;
    float N;
    float A;
    int   armour;
    float p;
    float eff_armour;
    float red;
    float vanilla_dmg;
    float new_dmg;
} DamageSample;

#define MAX_SAMPLES 16
static ArmourStats g_stats;
static DamageSample g_samples[MAX_SAMPLES];
static volatile LONG g_sample_idx = 0;

static void reset_stats(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_samples, 0, sizeof(g_samples));
    g_sample_idx = 0;
}

/* ---------- Core damage formula (called from assembly stubs) ---------- */

float rap_compute_armour_damage(
    int site_id,
    float N,
    float A,
    int armour,
    float kv_lower_cap,
    uint32_t *rng,
    int *zero_ap_out
) {
    /* Advance vanilla LCG RNG identically */
    uint32_t seed = (*rng) * 0x343fdU + 0x269ec3U;
    *rng = seed;
    float u = (float)(seed >> 16) * (1.0f / 65535.0f);

    if (site_id >= 0 && site_id < 3) {
        InterlockedIncrement64(&g_stats.calls[site_id]);
    }

    float a_raw = (armour > 0) ? (float)armour : 0.0f;
    float v_lo = a_raw * kv_lower_cap;
    float v_roll = v_lo + u * (a_raw - v_lo);
    if (v_roll < 0.0f) v_roll = 0.0f;
    if (v_roll > 100.0f) v_roll = 100.0f;
    float v_red = v_roll * 0.01f;
    float v_n_after = (1.0f - v_red) * N;
    if (v_n_after < 0.0f) v_n_after = 0.0f;
    float v_total = v_n_after + A;

    if (!g_cfg.enabled) {
        if (zero_ap_out) *zero_ap_out = 0;
        return (site_id == 0) ? v_n_after : v_total;
    }

    if (site_id >= 0 && site_id < 3) {
        InterlockedIncrement64(&g_stats.active_calls[site_id]);
    }

    float D = N + A;
    if (D <= 0.0f) {
        if (zero_ap_out) *zero_ap_out = 1;
        return 0.0f;
    }

    /* Step 1: penetration ratio p = (A / D)^gamma */
    float r = A / D;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    float p;
    if (r <= 0.0f) {
        p = 0.0f;
    } else if (r >= 1.0f) {
        p = 1.0f;
    } else {
        p = powf(r, g_cfg.gamma);
    }

    /* Step 2: effective armour */
    float eff_armour = a_raw * (1.0f - p);

    /* Step 3: uncapped linear armour roll & hyperbolic curve */
    float lower = g_cfg.use_kv_lower_cap ? kv_lower_cap : g_cfg.lower_cap;
    if (lower < 0.0f) lower = 0.0f;
    if (lower > 1.0f) lower = 1.0f;
    float lo = eff_armour * lower;
    float roll = lo + u * (eff_armour - lo);
    float red_lin = roll * 0.01f;
    if (red_lin < 0.0f) red_lin = 0.0f;
    if (red_lin > 1.0f) red_lin = 1.0f;

    float step = (g_cfg.hyp_step > 0.1f) ? g_cfg.hyp_step : 20.0f;
    float red_hyp = (eff_armour > 0.0f) ? (eff_armour / (eff_armour + step)) : 0.0f;

    float red;
    if (g_cfg.mode == 1) {
        red = red_lin;
    } else if (g_cfg.mode == 2) {
        red = red_hyp;
    } else {
        red = (red_lin < red_hyp) ? red_lin : red_hyp;
    }

    float new_dmg = D * (1.0f - red);
    if (new_dmg < 0.0f) new_dmg = 0.0f;
    if (g_cfg.min_damage > 0.0f && new_dmg < g_cfg.min_damage) {
        new_dmg = g_cfg.min_damage;
    }

    g_stats.sum_vanilla_dmg += (double)v_total;
    g_stats.sum_new_dmg += (double)new_dmg;
    g_stats.sum_pen_ratio += (double)p;
    g_stats.sum_eff_armour += (double)eff_armour;
    g_stats.sum_raw_armour += (double)a_raw;
    g_stats.sample_count++;

    LONG slot = InterlockedIncrement(&g_sample_idx) - 1;
    DamageSample *s = &g_samples[slot & (MAX_SAMPLES - 1)];
    s->site = site_id;
    s->N = N;
    s->A = A;
    s->armour = armour;
    s->p = p;
    s->eff_armour = eff_armour;
    s->red = red;
    s->vanilla_dmg = v_total;
    s->new_dmg = new_dmg;

    if (zero_ap_out) *zero_ap_out = 1;
    return new_dmg;
}

/* ---------- Hook return addresses (set at install time) ---------- */

void *g_ret_site0 = NULL;
void *g_ret_site1 = NULL;
void *g_ret_site2 = NULL;

extern void stub_site0(void);
extern void stub_site1(void);
extern void stub_site2(void);

__asm__(
    ".text\n"
    ".intel_syntax noprefix\n"

    ".globl stub_site0\n"
    "stub_site0:\n"
    "sub rsp, 0x80\n"
    "movaps [rsp+0x40], xmm3\n"
    "movaps [rsp+0x50], xmm4\n"
    "movaps [rsp+0x60], xmm5\n"
    "mov dword ptr [rsp+0x38], 0\n"
    "mov [rsp+0x28], r9\n"
    "movss [rsp+0x20], xmm6\n"
    "lea rax, [rsp+0x38]\n"
    "mov [rsp+0x30], rax\n"
    "xor ecx, ecx\n"
    "movaps xmm1, xmm9\n"
    "movaps xmm2, xmm8\n"
    "mov r9d, ebx\n"
    "call rap_compute_armour_damage\n"
    "movaps xmm7, xmm0\n"
    "cmp dword ptr [rsp+0x38], 0\n"
    "je .L_site0_keep_ap\n"
    "xorps xmm8, xmm8\n"
    ".L_site0_keep_ap:\n"
    "movaps xmm3, [rsp+0x40]\n"
    "movaps xmm4, [rsp+0x50]\n"
    "movaps xmm5, [rsp+0x60]\n"
    "add rsp, 0x80\n"
    "jmp qword ptr [rip + g_ret_site0]\n"

    ".globl stub_site1\n"
    "stub_site1:\n"
    "sub rsp, 0x60\n"
    "mov [rsp+0x28], r9\n"
    "movss [rsp+0x20], xmm6\n"
    "xor eax, eax\n"
    "mov [rsp+0x30], rax\n"
    "mov ecx, 1\n"
    "movss xmm1, [rsp+0xa0]\n"
    "movss xmm2, [rsp+0xa4]\n"
    "mov r9d, ebx\n"
    "call rap_compute_armour_damage\n"
    "add rsp, 0x60\n"
    "jmp qword ptr [rip + g_ret_site1]\n"

    ".globl stub_site2\n"
    "stub_site2:\n"
    "sub rsp, 0x60\n"
    "mov [rsp+0x28], r9\n"
    "movss [rsp+0x20], xmm6\n"
    "xor eax, eax\n"
    "mov [rsp+0x30], rax\n"
    "mov ecx, 2\n"
    "movss xmm1, [rsp+0x90]\n"
    "movss xmm2, [rsp+0x94]\n"
    "mov r9d, ebx\n"
    "call rap_compute_armour_damage\n"
    "add rsp, 0x60\n"
    "jmp qword ptr [rip + g_ret_site2]\n"

    ".att_syntax prefix\n"
);

/* ---------- Pattern scanning & inline patching ---------- */

typedef struct {
    const char *name;
    const uint8_t *pattern;
    const uint8_t *mask;
    size_t pat_len;
    size_t patch_len;
    void  *stub;
    void **ret_slot;
    uint8_t *addr;
    uint8_t orig_bytes[48];
    int installed;
} HookSite;

static const uint8_t PAT_SITE0[22] = {
    0xe8, 0x00, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x5c, 0xf8,
    0xf3, 0x41, 0x0f, 0x59, 0xf9,
    0xf3, 0x0f, 0x5f, 0x3d, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MASK_SITE0[22] = {
    1,0,0,0,0, 1,1,1,1, 1,1,1,1,1, 1,1,1,1,0,0,0,0
};

static const uint8_t PAT_SITE1[40] = {
    0xe8, 0x00, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x10, 0x1d, 0x00, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x5c, 0xd8,
    0xf3, 0x0f, 0x59, 0x5c, 0x24, 0x40,
    0xf3, 0x0f, 0x5f, 0x1d, 0x00, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x58, 0x5c, 0x24, 0x44,
    0x0f, 0x28, 0xc3
};
static const uint8_t MASK_SITE1[40] = {
    1,0,0,0,0, 1,1,1,1,0,0,0,0, 1,1,1,1, 1,1,1,1,1,1,
    1,1,1,1,0,0,0,0, 1,1,1,1,1,1, 1,1,1
};

static const uint8_t PAT_SITE2[40] = {
    0xe8, 0x00, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x10, 0x1d, 0x00, 0x00, 0x00, 0x00,
    0xf3, 0x0f, 0x5c, 0xd8,
    0x0f, 0x57, 0xc0,
    0xf3, 0x0f, 0x59, 0x5c, 0x24, 0x30,
    0x0f, 0x2f, 0xd8,
    0x72, 0x03,
    0x0f, 0x28, 0xc3,
    0xf3, 0x0f, 0x58, 0x44, 0x24, 0x34
};
static const uint8_t MASK_SITE2[40] = {
    1,0,0,0,0, 1,1,1,1,0,0,0,0, 1,1,1,1, 1,1,1,
    1,1,1,1,1,1, 1,1,1, 1,1, 1,1,1, 1,1,1,1,1,1
};

static HookSite g_sites[3] = {
    { "site0_struct", PAT_SITE0, MASK_SITE0, sizeof(PAT_SITE0), sizeof(PAT_SITE0), (void *)stub_site0, &g_ret_site0, NULL, {0}, 0 },
    { "site1_int_a",  PAT_SITE1, MASK_SITE1, sizeof(PAT_SITE1), sizeof(PAT_SITE1), (void *)stub_site1, &g_ret_site1, NULL, {0}, 0 },
    { "site2_int_b",  PAT_SITE2, MASK_SITE2, sizeof(PAT_SITE2), sizeof(PAT_SITE2), (void *)stub_site2, &g_ret_site2, NULL, {0}, 0 }
};

static int g_all_installed = 0;

static uint8_t *scan_executable_sections(const uint8_t *pat, const uint8_t *mask, size_t len, int *match_count) {
    HMODULE exe = GetModuleHandleA(NULL);
    if (!exe) return NULL;
    uint8_t *base = (uint8_t *)exe;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    uint8_t *found = NULL;
    int count = 0;

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        uint8_t *start = base + sec[i].VirtualAddress;
        size_t size = sec[i].Misc.VirtualSize;
        if (size < len) continue;

        size_t limit = size - len;
        for (size_t off = 0; off <= limit; off++) {
            if (mask[0] && start[off] != pat[0]) continue;
            size_t j = 1;
            for (; j < len; j++) {
                if (mask[j] && start[off + j] != pat[j]) break;
            }
            if (j == len) {
                found = start + off;
                count++;
                if (count > 1) {
                    *match_count = count;
                    return NULL;
                }
            }
        }
    }
    *match_count = count;
    return found;
}

static int patch_site(HookSite *s) {
    if (s->installed) return 1;
    if (!s->addr || s->patch_len < 14) return 0;

    memcpy(s->orig_bytes, s->addr, s->patch_len);
    *(s->ret_slot) = (void *)(s->addr + s->patch_len);

    DWORD old_prot = 0;
    if (!VirtualProtect(s->addr, s->patch_len, PAGE_EXECUTE_READWRITE, &old_prot)) {
        return 0;
    }

    uint8_t patch[48];
    memset(patch, 0x90, s->patch_len);
    patch[0] = 0xFF;
    patch[1] = 0x25;
    patch[2] = 0x00;
    patch[3] = 0x00;
    patch[4] = 0x00;
    patch[5] = 0x00;
    uint64_t target = (uint64_t)(uintptr_t)s->stub;
    memcpy(patch + 6, &target, 8);

    memcpy(s->addr, patch, s->patch_len);
    FlushInstructionCache(GetCurrentProcess(), s->addr, s->patch_len);
    VirtualProtect(s->addr, s->patch_len, old_prot, &old_prot);

    s->installed = 1;
    return 1;
}

static void unpatch_all(void) {
    g_cfg.enabled = 0;
    for (int i = 0; i < 3; i++) {
        HookSite *s = &g_sites[i];
        if (!s->installed || !s->addr) continue;
        DWORD old_prot = 0;
        if (VirtualProtect(s->addr, s->patch_len, PAGE_EXECUTE_READWRITE, &old_prot)) {
            memcpy(s->addr, s->orig_bytes, s->patch_len);
            FlushInstructionCache(GetCurrentProcess(), s->addr, s->patch_len);
            VirtualProtect(s->addr, s->patch_len, old_prot, &old_prot);
        }
        s->installed = 0;
    }
    g_all_installed = 0;
}

/* ---------- Lua bindings ---------- */

static int l_install(lua_State *L) {
    if (g_all_installed) {
        g_lua.pushboolean(L, 1);
        g_lua.pushstring(L, "already installed");
        return 2;
    }

    char msg[256];
    void *callee_target = NULL;
    for (int i = 0; i < 3; i++) {
        int cnt = 0;
        uint8_t *addr = scan_executable_sections(g_sites[i].pattern, g_sites[i].mask, g_sites[i].pat_len, &cnt);
        if (cnt != 1 || !addr) {
            snprintf(msg, sizeof(msg), "pattern mismatch on %s (matches=%d)", g_sites[i].name, cnt);
            g_lua.pushboolean(L, 0);
            g_lua.pushstring(L, msg);
            return 2;
        }
        int32_t rel = 0;
        memcpy(&rel, addr + 1, 4);
        void *dest = (void *)(addr + 5 + rel);
        if (callee_target == NULL) {
            callee_target = dest;
        } else if (callee_target != dest) {
            snprintf(msg, sizeof(msg), "callee mismatch on %s", g_sites[i].name);
            g_lua.pushboolean(L, 0);
            g_lua.pushstring(L, msg);
            return 2;
        }
        g_sites[i].addr = addr;
    }

    for (int i = 0; i < 3; i++) {
        if (!patch_site(&g_sites[i])) {
            unpatch_all();
            snprintf(msg, sizeof(msg), "VirtualProtect failed on %s", g_sites[i].name);
            g_lua.pushboolean(L, 0);
            g_lua.pushstring(L, msg);
            return 2;
        }
    }

    g_all_installed = 1;
    snprintf(msg, sizeof(msg), "ok s0=%p s1=%p s2=%p roll=%p",
             g_sites[0].addr, g_sites[1].addr, g_sites[2].addr, callee_target);
    g_lua.pushboolean(L, 1);
    g_lua.pushstring(L, msg);
    return 2;
}

static int l_uninstall(lua_State *L) {
    unpatch_all();
    g_lua.pushboolean(L, 1);
    return 1;
}

static int l_configure(lua_State *L) {
    int n = g_lua.gettop(L);
    if (n >= 1 && g_lua.type(L, 1) != 0) {
        g_cfg.enabled = (g_lua.tointeger(L, 1) != 0) ? 1 : 0;
    }
    if (n >= 2 && g_lua.type(L, 2) != 0) {
        float g = (float)g_lua.tonumber(L, 2);
        if (g >= 0.1f && g <= 5.0f) g_cfg.gamma = g;
    }
    if (n >= 3 && g_lua.type(L, 3) != 0) {
        float lc = (float)g_lua.tonumber(L, 3);
        if (lc >= 0.0f && lc <= 1.0f) g_cfg.lower_cap = lc;
    }
    if (n >= 4 && g_lua.type(L, 4) != 0) {
        int m = (int)g_lua.tointeger(L, 4);
        if (m >= 0 && m <= 2) g_cfg.mode = m;
    }
    if (n >= 5 && g_lua.type(L, 5) != 0) {
        float hs = (float)g_lua.tonumber(L, 5);
        if (hs >= 1.0f && hs <= 500.0f) g_cfg.hyp_step = hs;
    }
    if (n >= 6 && g_lua.type(L, 6) != 0) {
        float md = (float)g_lua.tonumber(L, 6);
        if (md >= 0.0f && md <= 100.0f) g_cfg.min_damage = md;
    }
    if (n >= 7 && g_lua.type(L, 7) != 0) {
        g_cfg.use_kv_lower_cap = (g_lua.tointeger(L, 7) != 0) ? 1 : 0;
    }
    g_lua.pushboolean(L, 1);
    return 1;
}

static int l_reset_stats(lua_State *L) {
    reset_stats();
    g_lua.pushboolean(L, 1);
    return 1;
}

static int l_diag_text(lua_State *L) {
    char buf[4096];
    int pos = 0;
    LONG64 sc = g_stats.sample_count;
    double avg_v = sc > 0 ? g_stats.sum_vanilla_dmg / (double)sc : 0.0;
    double avg_n = sc > 0 ? g_stats.sum_new_dmg / (double)sc : 0.0;
    double avg_p = sc > 0 ? (g_stats.sum_pen_ratio / (double)sc) * 100.0 : 0.0;
    double avg_a = sc > 0 ? g_stats.sum_raw_armour / (double)sc : 0.0;
    double avg_e = sc > 0 ? g_stats.sum_eff_armour / (double)sc : 0.0;
    const char *mode_name = (g_cfg.mode == 1) ? "linear" : ((g_cfg.mode == 2) ? "hyperbolic" : "combined");

    pos += snprintf(buf + pos, sizeof(buf) - pos,
        "Refined Armour Penetration v%d | installed=%d enabled=%d\n"
        "config: mode=%s gamma=%.2f lower_cap=%.2f hyp_step=%.1f min_dmg=%.1f use_kv_lower=%d\n"
        "calls:  s0=%I64d(act=%I64d) s1=%I64d(act=%I64d) s2=%I64d(act=%I64d)\n"
        "totals: samples=%I64d avg_vanilla=%.2f avg_new=%.2f avg_pen=%.1f%% avg_armour=%.1f->%.1f\n"
        "recent samples (site | N+A | arm | pen%% | eff_arm | red%% | vanilla -> new):\n",
        RAP_VERSION, g_all_installed, (int)g_cfg.enabled,
        mode_name, (double)g_cfg.gamma, (double)g_cfg.lower_cap, (double)g_cfg.hyp_step, (double)g_cfg.min_damage, g_cfg.use_kv_lower_cap,
        (long long)g_stats.calls[0], (long long)g_stats.active_calls[0],
        (long long)g_stats.calls[1], (long long)g_stats.active_calls[1],
        (long long)g_stats.calls[2], (long long)g_stats.active_calls[2],
        (long long)sc, avg_v, avg_n, avg_p, avg_a, avg_e);

    LONG idx = g_sample_idx;
    int show = (idx < 8) ? (int)idx : 8;
    for (int i = 0; i < show && pos < (int)sizeof(buf) - 128; i++) {
        DamageSample *s = &g_samples[(idx - 1 - i) & (MAX_SAMPLES - 1)];
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "  s%d | %.1f+%.1f | arm=%d | pen=%.1f%% | eff=%.1f | red=%.1f%% | %.1f -> %.1f\n",
            s->site, (double)s->N, (double)s->A, s->armour,
            (double)s->p * 100.0, (double)s->eff_armour, (double)s->red * 100.0,
            (double)s->vanilla_dmg, (double)s->new_dmg);
    }

    g_lua.pushstring(L, buf);
    return 1;
}

__declspec(dllexport) int luaopen_refined_armour_penetration_v1(lua_State *L) {
    if (!resolve_lua_api()) {
        return 0;
    }

    g_lua.createtable(L, 0, 8);

    g_lua.pushinteger(L, RAP_VERSION);
    g_lua.setfield(L, -2, "version");

    g_lua.pushcclosure(L, l_install, 0);
    g_lua.setfield(L, -2, "install");

    g_lua.pushcclosure(L, l_uninstall, 0);
    g_lua.setfield(L, -2, "uninstall");

    g_lua.pushcclosure(L, l_configure, 0);
    g_lua.setfield(L, -2, "configure");

    g_lua.pushcclosure(L, l_reset_stats, 0);
    g_lua.setfield(L, -2, "reset_stats");

    g_lua.pushcclosure(L, l_diag_text, 0);
    g_lua.setfield(L, -2, "diag_text");

    return 1;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    (void)hinst;
    (void)reserved;
    if (reason == DLL_PROCESS_DETACH) {
        unpatch_all();
    }
    return TRUE;
}
