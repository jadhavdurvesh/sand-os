#include "sand/security/launch_gate.h"
#include "sand/arch/x86/rtc.h"

static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }
void sand_audit(const char *text) {
    static const char prefix[] = "SAND_AUDIT:";
    for (const char *p = prefix; *p; ++p) outb(0xE9, (uint8_t)*p);
    for (; *text; ++text) outb(0xE9, (uint8_t)*text);
    outb(0xE9, '\n');
}

/* Built-in defaults: developer policy, no provisioned roots (nothing signed is trusted yet). */
static const struct sand_stci_trust_store default_store = { SAND_STCI_ABI_MAJOR, sizeof(struct sand_stci_trust_store), 0u, 1u, 0, 0, 0u };
static const struct sand_stci_policy default_policy = { SAND_STCI_ABI_MAJOR, sizeof(struct sand_stci_policy), SAND_STCI_POLICY_DEVELOPER_MODE, 0u, 0 };
static const struct sand_stci_trust_store *store = &default_store;
static const struct sand_stci_policy *policy = &default_policy;

void sand_launch_gate_configure(const struct sand_stci_policy *p, const struct sand_stci_trust_store *s) {
    policy = p ? p : &default_policy;
    store = s ? s : &default_store;
}
uint32_t sand_launch_gate_flags(void) { return policy->flags; }

static void audit2(const char *what, const char *name) {
    char line[80]; unsigned n = 0;
    for (; what[n] && n < 40u; ++n) line[n] = what[n];
    line[n++] = ' ';
    for (unsigned i = 0; name && name[i] && n < 78u; ++i) line[n++] = name[i];
    line[n] = 0; sand_audit(line);
}
static void put_hex8(char *o, const uint8_t *id) { static const char d[] = "0123456789abcdef"; for (int i = 0; i < 4; ++i) { o[2*i] = d[id[i] >> 4]; o[2*i+1] = d[id[i] & 15]; } }

int sand_launch_gate(const char *name, const void *image, uint32_t size) {
    const uint8_t *b = (const uint8_t *)image;
    if (!image || size < 8u || b[0] != 'S' || b[1] != 'A' || b[2] != 'P' || b[3] != 'P') { audit2("LAUNCH_DENIED_MALFORMED", name); return -1; }
    unsigned version = (unsigned)b[4] | ((unsigned)b[5] << 8);
    if (version == 1u) {
        if (policy->flags & SAND_STCI_POLICY_REQUIRE_SIGNED) { audit2("LAUNCH_DENIED_UNSIGNED", name); return -1; }
        audit2("LAUNCH_UNSIGNED_DEVELOPER", name);
        return 0;
    }
    if (version != 2u) { audit2("LAUNCH_DENIED_VERSION", name); return -1; }
    struct sand_stci_policy local = *policy; char now[20];
    if (!local.now_utc) { if (rtc_read_iso(now) != 0) now[0] = 0; local.now_utc = now[0] ? now : 0; }
    uint8_t digest[SAND_STCI_SHA256_SIZE]; struct sand_stci_result res;
    int st = sand_stci_verify_sapp_v2_ex(image, size, store, &local, digest, &res);
    if (st != SAND_STCI_OK) {
        char line[40] = "LAUNCH_DENIED_SIGNATURE status=-"; unsigned n = 32u; int v = -st;
        if (v >= 10) { line[n++] = (char)('0' + v / 10); }
        line[n++] = (char)('0' + v % 10); line[n] = 0;
        audit2(line, name); return -1;
    }
    char line[64] = "LAUNCH_SIGNED pub="; unsigned n = 18u; put_hex8(line + n, res.publisher_key_id); n += 8u;
    line[n++] = ' '; line[n++] = 'r'; line[n++] = '='; put_hex8(line + n, res.root_key_id); n += 8u; line[n] = 0;
    audit2(line, name);
    return 0;
}
