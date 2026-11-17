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
    sb_str(&s, "Try: open <app> | apps | time | status | settings | note <text> | notes | remind me in 5 minutes to <text> | reminders | set accent <name>");
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

/* ---- notes and reminders ---- */
/* Joins tokens[from..] with single spaces; -1 when it does not fit. */
static int join_tokens(const CitadelCommand *c, uint32_t from, char *out, size_t cap) {
    size_t n = 0; uint32_t i;
    for (i = from; i < c->token_count; ++i) {
        size_t len = c->tokens[i].length;
        if (n + len + (n ? 1u : 0u) + 1 > cap) return -1;
        if (n) out[n++] = ' ';
        memcpy(out + n, c->tokens[i].data, len); n += len;
    }
    out[n] = '\0'; return (int)n;
}
static int parse_uint(const CitadelCommand *c, uint32_t i, uint32_t *out) {
    uint32_t v = 0, k;
    if (i >= c->token_count || c->tokens[i].length == 0 || c->tokens[i].length > 6) return -1;
    for (k = 0; k < c->tokens[i].length; ++k) { char d = c->tokens[i].data[k]; if (d < '0' || d > '9') return -1; v = v * 10u + (uint32_t)(d - '0'); }
    *out = v; return 0;
}
static CitadelStatus cap_note(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); char text[CITADEL_SAND_NOTE_MAX + 1]; Sb s; sb_init(&s, r);
    if (!p->note_add) { sb_str(&s, "Notes are not available here."); return CITADEL_ERR_UNAVAILABLE; }
    if (join_tokens(c, 1, text, sizeof(text)) < 0) { sb_str(&s, "That note is too long (60 characters at most)."); return CITADEL_ERR_INVALID_ARGUMENT; }
    if (p->note_add(p->context, text) != 0) { sb_str(&s, "Notes are full. Say 'clear notes' to start over."); return CITADEL_ERR_LIMIT; }
    sb_str(&s, "Noted: "); sb_str(&s, text);
    return CITADEL_OK;
}
static CitadelStatus cap_notes(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); char text[CITADEL_SAND_NOTE_MAX + 1]; uint32_t i, n; Sb s; (void)c; sb_init(&s, r);
    if (!p->note_count || !p->note_get) { sb_str(&s, "Notes are not available here."); return CITADEL_ERR_UNAVAILABLE; }
    n = p->note_count(p->context);
    if (n == 0) { sb_str(&s, "You have no notes. Say 'note <text>' to add one."); return CITADEL_OK; }
    for (i = 0; i < n; ++i) {
        if (p->note_get(p->context, i, text, sizeof(text)) != 0) continue;
        if (i) sb_str(&s, " | ");
        sb_uint(&s, i + 1u, 1); sb_str(&s, ") "); sb_str(&s, text);
    }
    return CITADEL_OK;
}
static CitadelStatus cap_notes_clear(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); Sb s; (void)c; sb_init(&s, r);
    if (!p->notes_clear || p->notes_clear(p->context) != 0) { sb_str(&s, "I could not clear the notes."); return CITADEL_ERR_UNAVAILABLE; }
    sb_str(&s, "All notes were deleted."); return CITADEL_OK;
}
/* remind me in <n> <second|minute|hour>[s] [to <text...>] */
static CitadelStatus cap_remind(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); uint32_t n, unit = 0, seconds; char text[CITADEL_SAND_NOTE_MAX + 1]; Sb s; sb_init(&s, r);
    static const char *const SYNTAX = "Say 'remind me in 10 minutes to stretch' (seconds, minutes or hours; up to 24 hours).";
    if (!p->reminder_add) { sb_str(&s, "Reminders are not available here."); return CITADEL_ERR_UNAVAILABLE; }
    if (!tok_is(c, 1, "me") || !tok_is(c, 2, "in") || parse_uint(c, 3, &n) != 0 || n == 0) { sb_str(&s, SYNTAX); return CITADEL_ERR_INVALID_ARGUMENT; }
    if (tok_is(c, 4, "second") || tok_is(c, 4, "seconds") || tok_is(c, 4, "sec") || tok_is(c, 4, "s")) unit = 1;
    else if (tok_is(c, 4, "minute") || tok_is(c, 4, "minutes") || tok_is(c, 4, "min") || tok_is(c, 4, "m")) unit = 60;
    else if (tok_is(c, 4, "hour") || tok_is(c, 4, "hours") || tok_is(c, 4, "h")) unit = 3600;
    if (unit == 0 || n > CITADEL_SAND_REMINDER_MAX_SECONDS) { sb_str(&s, SYNTAX); return CITADEL_ERR_INVALID_ARGUMENT; }
    seconds = n * unit;
    if (seconds > CITADEL_SAND_REMINDER_MAX_SECONDS) { sb_str(&s, SYNTAX); return CITADEL_ERR_INVALID_ARGUMENT; }
    text[0] = '\0';
    if (c->token_count > 5) {
        if (!tok_is(c, 5, "to") || c->token_count < 7 || join_tokens(c, 6, text, sizeof(text)) < 0) { sb_str(&s, SYNTAX); return CITADEL_ERR_INVALID_ARGUMENT; }
    }
    if (p->reminder_add(p->context, seconds, text) != 0) { sb_str(&s, "I am already holding as many reminders as I can. Say 'clear reminders'."); return CITADEL_ERR_LIMIT; }
    sb_str(&s, "Okay, I will remind you in "); sb_uint(&s, n, 1); sb_str(&s, unit == 1 ? " second" : unit == 60 ? " minute" : " hour"); if (n != 1) sb_str(&s, "s");
    if (text[0]) { sb_str(&s, ": "); sb_str(&s, text); } else sb_str(&s, ".");
    return CITADEL_OK;
}
static CitadelStatus cap_reminders(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); char text[CITADEL_SAND_NOTE_MAX + 1]; uint32_t i, n, left; Sb s; (void)c; sb_init(&s, r);
    if (!p->reminder_count || !p->reminder_get) { sb_str(&s, "Reminders are not available here."); return CITADEL_ERR_UNAVAILABLE; }
    n = p->reminder_count(p->context);
    if (n == 0) { sb_str(&s, "No reminders are waiting."); return CITADEL_OK; }
    for (i = 0; i < n; ++i) {
        if (p->reminder_get(p->context, i, &left, text, sizeof(text)) != 0) continue;
        if (i) sb_str(&s, " | ");
        if (left >= 3600u) { sb_uint(&s, left / 3600u, 1); sb_str(&s, "h "); sb_uint(&s, (left / 60u) % 60u, 1); sb_str(&s, "m"); }
        else if (left >= 60u) { sb_uint(&s, left / 60u, 1); sb_str(&s, "m "); sb_uint(&s, left % 60u, 1); sb_str(&s, "s"); }
        else { sb_uint(&s, left, 1); sb_str(&s, "s"); }
        sb_str(&s, ": "); sb_str(&s, text[0] ? text : "(timer)");
    }
    return CITADEL_OK;
}
static CitadelStatus cap_reminders_clear(const CitadelCommand *c, CitadelResult *r, void *u) {
    const CitadelSandPlatform *p = P(u); Sb s; (void)c; sb_init(&s, r);
    if (!p->reminders_clear || p->reminders_clear(p->context) != 0) { sb_str(&s, "I could not clear the reminders."); return CITADEL_ERR_UNAVAILABLE; }
    sb_str(&s, "All reminders were cancelled."); return CITADEL_OK;
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
        { "sand.set", "set *", "sand.set" },
        { "sand.note", "note *", "sand.note" }, { "sand.notes", "notes", "sand.notes" }, { "sand.notes2", "show notes", "sand.notes" },
        { "sand.notes_clear", "clear notes", "sand.notes_clear" },
        { "sand.remind", "remind *", "sand.remind" }, { "sand.reminders", "reminders", "sand.reminders" }, { "sand.reminders_clear", "clear reminders", "sand.reminders_clear" }
    };
    size_t i;
    if (reg == NULL || p == NULL) return CITADEL_ERR_INVALID_ARGUMENT;
    if (add_cap(reg, "sand.help", CITADEL_PERMISSION_NONE, 0, cap_help, NULL) || add_cap(reg, "sand.apps", CITADEL_PERMISSION_LAUNCH_APP, 0, cap_apps, (void *)p) ||
        add_cap(reg, "sand.open", CITADEL_PERMISSION_LAUNCH_APP, 0, cap_open, (void *)p) || add_cap(reg, "sand.time", CITADEL_PERMISSION_READ_SYSTEM, 0, cap_time, (void *)p) ||
        add_cap(reg, "sand.status", CITADEL_PERMISSION_READ_SYSTEM, 0, cap_status, (void *)p) || add_cap(reg, "sand.show_settings", CITADEL_PERMISSION_READ_SYSTEM, 0, cap_show_settings, (void *)p) ||
        add_cap(reg, "sand.set", CITADEL_PERMISSION_MODIFY_SETTINGS, 1, cap_set, (void *)p) ||
        add_cap(reg, "sand.note", CITADEL_PERMISSION_FILE_ACCESS, 0, cap_note, (void *)p) || add_cap(reg, "sand.notes", CITADEL_PERMISSION_FILE_ACCESS, 0, cap_notes, (void *)p) ||
        add_cap(reg, "sand.notes_clear", CITADEL_PERMISSION_FILE_ACCESS, 1, cap_notes_clear, (void *)p) ||
        add_cap(reg, "sand.remind", CITADEL_PERMISSION_SCHEDULE, 0, cap_remind, (void *)p) || add_cap(reg, "sand.reminders", CITADEL_PERMISSION_SCHEDULE, 0, cap_reminders, (void *)p) ||
        add_cap(reg, "sand.reminders_clear", CITADEL_PERMISSION_SCHEDULE, 0, cap_reminders_clear, (void *)p)) return CITADEL_ERR_INTERNAL;
    for (i = 0; i < sizeof(intents) / sizeof(intents[0]); ++i)
        if (citadel_register_intent(reg, intents[i].id, intents[i].phrase, intents[i].cap) != CITADEL_OK) return CITADEL_ERR_INTERNAL;
    return CITADEL_OK;
}
