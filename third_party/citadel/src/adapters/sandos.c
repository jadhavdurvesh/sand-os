#include "citadel/sandos.h"
#include <string.h>

/* ---- tiny bounded text builder (no snprintf: the adapter must link without a C library) ---- */
typedef struct Sb { char *buf; size_t cap, len; int overflow; } Sb;
static void sb_init(Sb *s, CitadelResult *r) { s->buf = r->text; s->cap = sizeof(r->text); s->len = 0; s->overflow = 0; s->buf[0] = '\0'; }
static void sb_str(Sb *s, const char *t) {
    size_t n = strlen(t);
    if (s->len + n + 1 > s->cap) { n = s->cap - s->len - 1; s->overflow = 1; }
    memcpy(s->buf + s->len, t, n); s->len += n; s->buf[s->len] = '\0';
}
static void sb_uint(Sb *s, uint32_t v, unsigned min_digits) {
    char tmp[11]; unsigned n = 0, i;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < sizeof(tmp));
    while (n < min_digits && n < sizeof(tmp)) tmp[n++] = '0';
    { char out[11]; for (i = 0; i < n; ++i) out[i] = tmp[n - 1 - i]; out[n] = '\0'; sb_str(s, out); }
}

static const CitadelSandPlatform *P(void *u) { return (const CitadelSandPlatform *)u; }
static int tok_is(const CitadelCommand *c, uint32_t i, const char *word) {
    size_t n = strlen(word);
    return i < c->token_count && c->tokens[i].length == n && memcmp(c->tokens[i].data, word, n) == 0;
}
static int tok_copy(const CitadelCommand *c, uint32_t i, char *out, size_t cap) {
    if (i >= c->token_count || c->tokens[i].length + 1 > cap) return -1;
    memcpy(out, c->tokens[i].data, c->tokens[i].length); out[c->tokens[i].length] = '\0'; return 0;
}

static const char *const ACCENTS[CITADEL_SAND_ACCENT_COUNT] = { "champagne", "glacier", "violet", "jade", "ember" };
static const struct { const char *alias, *app; } ALIASES[] = {
    { "terminal", "cairn" }, { "settings", "keystone" }, { "stopwatch", "chronos" }, { "files", "locus" }
};

/* ---- capabilities ---- */
static CitadelStatus cap_help(const CitadelCommand *c, CitadelResult *r, void *u) {
    Sb s; (void)c; (void)u; sb_init(&s, r);
    sb_str(&s, "Try: open <app> | apps | time | date | status | uptime | settings | set accent <name> | set motion on|off | set clock 12|24");
    return CITADEL_OK;
}
static CitadelStatus cap_apps(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); Sb s; uint32_t i, n; char name[24]; (void)c;
    sb_init(&s, r);
    if (!p->app_count || !p->app_name) return CITADEL_ERR_UNAVAILABLE;
    n = p->app_count(p->context);
    sb_str(&s, "Apps:");
    for (i = 0; i < n; ++i) {
        if (p->app_name(p->context, i, name, sizeof(name)) != 0) continue;
        if (s.len + strlen(name) + 6 > s.cap) { sb_str(&s, " ..."); break; }
        sb_str(&s, " "); sb_str(&s, name);
    }
    if (n == 0) sb_str(&s, " none");
    return CITADEL_OK;
}
static CitadelStatus cap_open(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); char name[24]; Sb s; size_t i; int rc;
    if (!p->launch || tok_copy(c, 1, name, sizeof(name)) != 0) { sb_init(&s, r); sb_str(&s, "I could not read that app name."); return CITADEL_ERR_INVALID_ARGUMENT; }
    for (i = 0; i < sizeof(ALIASES) / sizeof(ALIASES[0]); ++i)
        if (strcmp(name, ALIASES[i].alias) == 0) { strcpy(name, ALIASES[i].app); break; }
    sb_init(&s, r);
    rc = p->launch(p->context, name);
    if (rc < 0) { sb_str(&s, "I could not open "); sb_str(&s, name); sb_str(&s, ". Say 'apps' to see what is available."); return CITADEL_ERR_NOT_FOUND; }
    sb_str(&s, "Opening "); sb_str(&s, name); sb_str(&s, ".");
    return CITADEL_OK;
}
static CitadelStatus cap_time(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); CitadelSandTime t; Sb s; int date_only = tok_is(c, 0, "date") || tok_is(c, 2, "date");
    static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    sb_init(&s, r);
    if (!p->wall_time || p->wall_time(p->context, &t) != 0 || t.month < 1u || t.month > 12u) { sb_str(&s, "I cannot read the clock right now."); return CITADEL_ERR_UNAVAILABLE; }
    if (date_only) { sb_uint(&s, t.day, 1); sb_str(&s, " "); sb_str(&s, MONTHS[t.month - 1u]); sb_str(&s, " "); sb_uint(&s, t.year, 4); }
    else { sb_str(&s, "It is "); sb_uint(&s, t.hour, 2); sb_str(&s, ":"); sb_uint(&s, t.minute, 2); sb_str(&s, " on "); sb_uint(&s, t.day, 1); sb_str(&s, " "); sb_str(&s, MONTHS[t.month - 1u]); sb_str(&s, " "); sb_uint(&s, t.year, 4); sb_str(&s, " (the machine's clock, not synchronised)."); }
    return CITADEL_OK;
}
static CitadelStatus cap_status(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); CitadelSandInfo i; Sb s; int up = tok_is(c, 0, "uptime"); sb_init(&s, r);
    if (!p->system_info || p->system_info(p->context, &i) != 0) { sb_str(&s, "System information is unavailable."); return CITADEL_ERR_UNAVAILABLE; }
    if (up) { sb_str(&s, "Up for "); sb_uint(&s, i.uptime_seconds / 3600u, 1); sb_str(&s, "h "); sb_uint(&s, (i.uptime_seconds / 60u) % 60u, 2); sb_str(&s, "m "); sb_uint(&s, i.uptime_seconds % 60u, 2); sb_str(&s, "s."); return CITADEL_OK; }
    sb_str(&s, "Memory: "); sb_uint(&s, i.free_kib / 1024u, 1); sb_str(&s, " of "); sb_uint(&s, i.total_kib / 1024u, 1); sb_str(&s, " MiB free | processes: "); sb_uint(&s, i.process_count, 1);
    sb_str(&s, " | up "); sb_uint(&s, i.uptime_seconds / 60u, 1); sb_str(&s, " min");
    return CITADEL_OK;
}
static CitadelStatus cap_show_settings(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); int a, m, k; Sb s; (void)c; sb_init(&s, r);
    if (!p->setting_get) return CITADEL_ERR_UNAVAILABLE;
    a = p->setting_get(p->context, CITADEL_SAND_SETTING_ACCENT); m = p->setting_get(p->context, CITADEL_SAND_SETTING_REDUCED_MOTION); k = p->setting_get(p->context, CITADEL_SAND_SETTING_CLOCK_24H);
    if (a < 0 || a >= (int)CITADEL_SAND_ACCENT_COUNT || m < 0 || k < 0) { sb_str(&s, "I cannot read the settings."); return CITADEL_ERR_UNAVAILABLE; }
    sb_str(&s, "Accent: "); sb_str(&s, ACCENTS[a]); sb_str(&s, " | reduced motion: "); sb_str(&s, m ? "on" : "off"); sb_str(&s, " | clock: "); sb_str(&s, k ? "24-hour" : "12-hour");
    return CITADEL_OK;
}
/* set accent <name> | set motion on|off | set clock 12|24 -- reached only with MODIFY_SETTINGS and confirmation. */
static CitadelStatus cap_set(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); Sb s; uint32_t i; sb_init(&s, r);
    if (!p->setting_set || c->token_count != 3) { sb_str(&s, "Say 'set accent <name>', 'set motion on|off' or 'set clock 12|24'."); return CITADEL_ERR_INVALID_ARGUMENT; }
    if (tok_is(c, 1, "accent")) {
        for (i = 0; i < CITADEL_SAND_ACCENT_COUNT; ++i)
            if (tok_is(c, 2, ACCENTS[i])) {
                if (p->setting_set(p->context, CITADEL_SAND_SETTING_ACCENT, i) != 0) { sb_str(&s, "The system refused that change."); return CITADEL_ERR_DENIED; }
                sb_str(&s, "Accent is now "); sb_str(&s, ACCENTS[i]); sb_str(&s, "."); return CITADEL_OK;
            }
        sb_str(&s, "Accents: champagne, glacier, violet, jade, ember."); return CITADEL_ERR_INVALID_ARGUMENT;
    }
    if (tok_is(c, 1, "motion") && (tok_is(c, 2, "on") || tok_is(c, 2, "off"))) {
        int on = tok_is(c, 2, "on");
        if (p->setting_set(p->context, CITADEL_SAND_SETTING_REDUCED_MOTION, (uint32_t)on) != 0) { sb_str(&s, "The system refused that change."); return CITADEL_ERR_DENIED; }
        sb_str(&s, "Reduced motion is "); sb_str(&s, on ? "on." : "off."); return CITADEL_OK;
    }
    if (tok_is(c, 1, "clock") && (tok_is(c, 2, "12") || tok_is(c, 2, "24"))) {
        int h24 = tok_is(c, 2, "24");
        if (p->setting_set(p->context, CITADEL_SAND_SETTING_CLOCK_24H, (uint32_t)h24) != 0) { sb_str(&s, "The system refused that change."); return CITADEL_ERR_DENIED; }
        sb_str(&s, "Clock is now "); sb_str(&s, h24 ? "24-hour." : "12-hour."); return CITADEL_OK;
    }
    sb_str(&s, "Say 'set accent <name>', 'set motion on|off' or 'set clock 12|24'."); return CITADEL_ERR_INVALID_ARGUMENT;
}

static CitadelStatus add_cap(CitadelRegistry *reg, const char *id, uint32_t perms, uint8_t confirm, CitadelCapabilityFn fn, void *user) {
    CitadelCapability d; memset(&d, 0, sizeof(d));
    if (strlen(id) >= sizeof(d.id)) return CITADEL_ERR_INTERNAL;
    memcpy(d.id, id, strlen(id) + 1); d.required_permissions = perms; d.requires_confirmation = confirm;
    return citadel_register_capability(reg, &d, fn, user);
}
CitadelStatus citadel_sandos_register(CitadelRegistry *reg, const CitadelSandPlatform *p) {
    static const struct { const char *id, *phrase, *cap; } intents[] = {
        { "sand.help", "help", "sand.help" }, { "sand.help2", "what can you do", "sand.help" },
        { "sand.apps", "apps", "sand.apps" }, { "sand.apps2", "list apps", "sand.apps" }, { "sand.apps3", "show apps", "sand.apps" },
        { "sand.open", "open *", "sand.open" }, { "sand.launch", "launch *", "sand.open" }, { "sand.start", "start *", "sand.open" },
        { "sand.time", "time", "sand.time" }, { "sand.time2", "what time is it", "sand.time" },
        { "sand.date", "date", "sand.time" }, { "sand.date2", "what is the date", "sand.time" },
        { "sand.status", "status", "sand.status" }, { "sand.status2", "system status", "sand.status" }, { "sand.uptime", "uptime", "sand.status" },
        { "sand.settings", "settings", "sand.show_settings" }, { "sand.settings2", "show settings", "sand.show_settings" },
        { "sand.set", "set *", "sand.set" }
    };
    size_t i;
    if (reg == NULL || p == NULL) return CITADEL_ERR_INVALID_ARGUMENT;
    if (add_cap(reg, "sand.help", CITADEL_PERMISSION_NONE, 0, cap_help, NULL) || add_cap(reg, "sand.apps", CITADEL_PERMISSION_LAUNCH_APP, 0, cap_apps, (void *)p) ||
        add_cap(reg, "sand.open", CITADEL_PERMISSION_LAUNCH_APP, 0, cap_open, (void *)p) || add_cap(reg, "sand.time", CITADEL_PERMISSION_READ_SYSTEM, 0, cap_time, (void *)p) ||
        add_cap(reg, "sand.status", CITADEL_PERMISSION_READ_SYSTEM, 0, cap_status, (void *)p) || add_cap(reg, "sand.show_settings", CITADEL_PERMISSION_READ_SYSTEM, 0, cap_show_settings, (void *)p) ||
        add_cap(reg, "sand.set", CITADEL_PERMISSION_MODIFY_SETTINGS, 1, cap_set, (void *)p)) return CITADEL_ERR_INTERNAL;
    for (i = 0; i < sizeof(intents) / sizeof(intents[0]); ++i)
        if (citadel_register_intent(reg, intents[i].id, intents[i].phrase, intents[i].cap) != CITADEL_OK) return CITADEL_ERR_INTERNAL;
    return CITADEL_OK;
}
