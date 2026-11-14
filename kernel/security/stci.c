#include "sand/security/stci.h"
#include "sand/crypto/ed25519.h"

int sand_stci_ed25519_verify(const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
                             const uint8_t *message, uint32_t message_size,
                             const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]) {
    if (!public_key || !signature || (message_size && !message)) return 0;
    return sand_ed25519_verify(signature, public_key, message, message_size);
}

int sand_stci_verify_sapp_v2_signature(const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
                                       const void *image, uint32_t image_size, uint32_t signature_offset,
                                       const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]) {
    if (!public_key || !image || !signature) return SAND_STCI_ERR_ARGUMENT;
    if (image_size == 0u || image_size > SAND_STCI_MAX_IMAGE_SIZE) return SAND_STCI_ERR_FORMAT;
    if (signature_offset == 0u || signature_offset > image_size) return SAND_STCI_ERR_FORMAT;
    if (!sand_ed25519_verify2(signature, public_key,
                              SAND_STCI_SAPP_V2_DOMAIN, SAND_STCI_SAPP_V2_DOMAIN_SIZE,
                              image, signature_offset))
        return SAND_STCI_ERR_SIGNATURE;
    return SAND_STCI_OK;
}

/* ======================================================================
 * SAPP v2 full verification: signature block -> publisher certificate ->
 * provisioned root. Everything is bounds-checked and fails closed.
 * ====================================================================== */
#include "sand/crypto/sha256.h"

#define SAPP_HEADER_SIZE 64u
#define SAPP_SEGMENT_SIZE 32u
#define SAPP_RELOC_SIZE 12u
#define SIG_BLOCK_MAX 8192u

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static int in_range(uint32_t off, uint32_t len, uint32_t total) { return off <= total && len <= total - off; }
static int mem_eq(const uint8_t *a, const uint8_t *b, uint32_t n) { uint8_t d = 0; for (uint32_t i = 0; i < n; ++i) d |= a[i] ^ b[i]; return d == 0; }

/* ---- strict canonical JSON: sorted keys, no whitespace, ints and strings only ---- */
struct jp { const uint8_t *b; uint32_t n, i; };
struct span { uint32_t start, end; };            /* [start,end) of a value's JSON text */

static int jp_string(struct jp *p) {             /* at opening quote; leaves p after closing quote */
    if (p->i >= p->n || p->b[p->i] != '"') return -1;
    ++p->i;
    while (p->i < p->n) {
        uint8_t c = p->b[p->i++];
        if (c == '"') return 0;
        if (c < 0x20u) return -1;
        if (c == '\\') { if (p->i >= p->n || p->b[p->i] != 'n') return -1; ++p->i; }   /* only \n (PEM) is accepted */
    }
    return -1;
}
static int jp_value(struct jp *p, int depth, const char *want, struct span *found);
static int key_cmp(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl) {
    uint32_t n = al < bl ? al : bl;
    for (uint32_t i = 0; i < n; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return al < bl ? -1 : (al > bl ? 1 : 0);
}
static int str_is(const struct jp *p, const char *lit, uint32_t ks, uint32_t ke) {   /* key text between quotes */
    uint32_t i = 0; for (; lit[i]; ++i) if (ks + i >= ke || p->b[ks + i] != (uint8_t)lit[i]) return 0;
    return ks + i == ke;
}
/* Parse an object. When `want` is given, record the value span of that member (and member start). */
static int jp_object(struct jp *p, int depth, const char *want, struct span *found, struct span *member) {
    if (depth > 4 || p->i >= p->n || p->b[p->i] != '{') return -1;
    ++p->i;
    if (p->i < p->n && p->b[p->i] == '}') { ++p->i; return 0; }
    uint32_t prev_s = 0, prev_e = 0; int have_prev = 0;
    for (;;) {
        uint32_t mstart = p->i;
        if (jp_string(p) != 0) return -1;
        uint32_t ks = mstart + 1, ke = p->i - 1;
        if (have_prev && key_cmp(p->b + prev_s, prev_e - prev_s, p->b + ks, ke - ks) >= 0) return -1;   /* sorted, unique */
        prev_s = ks; prev_e = ke; have_prev = 1;
        if (p->i >= p->n || p->b[p->i] != ':') return -1;
        ++p->i;
        struct span v; v.start = p->i;
        int is_want = want && str_is(p, want, ks, ke);
        if (jp_value(p, depth + 1, 0, 0) != 0) return -1;
        v.end = p->i;
        if (is_want) { if (found) *found = v; if (member) { member->start = mstart; member->end = v.end; } }
        if (p->i >= p->n) return -1;
        if (p->b[p->i] == ',') { ++p->i; continue; }
        if (p->b[p->i] == '}') { ++p->i; return 0; }
        return -1;
    }
}
static int jp_value(struct jp *p, int depth, const char *want, struct span *found) {
    (void)want; (void)found;
    if (depth > 5 || p->i >= p->n) return -1;
    uint8_t c = p->b[p->i];
    if (c == '"') return jp_string(p);
    if (c == '{') return jp_object(p, depth, 0, 0, 0);
    if (c == '[') {
        ++p->i;
        if (p->i < p->n && p->b[p->i] == ']') { ++p->i; return 0; }
        for (;;) {
            if (jp_value(p, depth + 1, 0, 0) != 0) return -1;
            if (p->i >= p->n) return -1;
            if (p->b[p->i] == ',') { ++p->i; continue; }
            if (p->b[p->i] == ']') { ++p->i; return 0; }
            return -1;
        }
    }
    if (c >= '0' && c <= '9') {                              /* canonical non-negative integer */
        uint32_t s = p->i;
        while (p->i < p->n && p->b[p->i] >= '0' && p->b[p->i] <= '9') ++p->i;
        if (p->i - s > 1 && p->b[s] == '0') return -1;
        return 0;
    }
    return -1;                                               /* true/false/null/floats/negatives are not used */
}
/* Validates `buf` as ONE canonical object and locates `key`. */
static int obj_find(const uint8_t *buf, uint32_t len, const char *key, struct span *value, struct span *member) {
    struct jp p = { buf, len, 0 };
    struct span v = { 0, 0 }, m = { 0, 0 };
    if (jp_object(&p, 0, key, &v, &m) != 0 || p.i != len) return -1;
    if (v.end == v.start) return -1;                         /* member missing */
    *value = v; if (member) *member = m;
    return 0;
}
/* A string value span -> pointer/length of its raw (unescaped-as-text) contents. */
static int str_body(const uint8_t *buf, struct span v, const uint8_t **s, uint32_t *n) {
    if (v.end - v.start < 2 || buf[v.start] != '"' || buf[v.end - 1] != '"') return -1;
    *s = buf + v.start + 1; *n = v.end - v.start - 2; return 0;
}
static int str_eq(const uint8_t *buf, struct span v, const char *lit) {
    const uint8_t *s; uint32_t n; if (str_body(buf, v, &s, &n) != 0) return 0;
    uint32_t i = 0; for (; lit[i]; ++i) if (i >= n || s[i] != (uint8_t)lit[i]) return 0;
    return i == n;
}
static int int_eq(const uint8_t *buf, struct span v, uint32_t expect) {
    uint32_t x = 0; if (v.end == v.start) return 0;
    for (uint32_t i = v.start; i < v.end; ++i) { if (buf[i] < '0' || buf[i] > '9' || x > 100000u) return 0; x = x * 10u + (uint32_t)(buf[i] - '0'); }
    return x == expect;
}

/* ---- strict base64 (standard alphabet, padded) ---- */
static int b64val(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static int b64_decode(const uint8_t *s, uint32_t n, uint8_t *out, uint32_t out_len) {   /* exact output length */
    if (n == 0 || n % 4u) return -1;
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 4) {
        int v[4]; int pad = 0;
        for (int k = 0; k < 4; ++k) {
            if (s[i + k] == '=') { if (i + 4 != n || k < 2) return -1; v[k] = 0; ++pad; }
            else { if (pad) return -1; v[k] = b64val(s[i + k]); if (v[k] < 0) return -1; }
        }
        uint32_t w = ((uint32_t)v[0] << 18) | ((uint32_t)v[1] << 12) | ((uint32_t)v[2] << 6) | (uint32_t)v[3];
        uint8_t b[3] = { (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w };
        if (pad == 2 && (b[1] || b[2])) return -1;               /* non-zero trailing bits */
        if (pad == 1 && b[2]) return -1;
        for (int k = 0; k < 3 - pad; ++k) { if (o >= out_len) return -1; out[o++] = b[k]; }
    }
    return o == out_len ? 0 : -1;
}

static void hex_of(const uint8_t *in, char out[65]) {
    static const char d[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) { out[2*i] = d[in[i] >> 4]; out[2*i+1] = d[in[i] & 15]; }
    out[64] = 0;
}

/* PEM "-----BEGIN PUBLIC KEY-----\n<base64 SPKI>\n-----END PUBLIC KEY-----\n" as embedded in canonical JSON
 * (newlines appear as the two characters backslash-n). Extracts the raw Ed25519 key and its key ID. */
static int parse_pem_spki(const uint8_t *s, uint32_t n, uint8_t key[32], uint8_t key_id[32]) {
    static const char head[] = "-----BEGIN PUBLIC KEY-----\\n", tail[] = "\\n-----END PUBLIC KEY-----\\n";
    uint32_t hl = sizeof(head) - 1u, tl = sizeof(tail) - 1u;
    if (n != hl + 60u + tl) return -1;                       /* 44 DER bytes -> 60 base64 chars */
    for (uint32_t i = 0; i < hl; ++i) if (s[i] != (uint8_t)head[i]) return -1;
    for (uint32_t i = 0; i < tl; ++i) if (s[hl + 60u + i] != (uint8_t)tail[i]) return -1;
    static const uint8_t spki_prefix[12] = { 0x30,0x2a,0x30,0x05,0x06,0x03,0x2b,0x65,0x70,0x03,0x21,0x00 };
    uint8_t der[44];
    if (b64_decode(s + hl, 60u, der, 44u) != 0) return -1;
    for (int i = 0; i < 12; ++i) if (der[i] != spki_prefix[i]) return -1;    /* Ed25519 SubjectPublicKeyInfo only */
    for (int i = 0; i < 32; ++i) key[i] = der[12 + i];
    sand_sha256(der, 44u, key_id);
    return 0;
}

/* Fixed-format UTC timestamp check: returns 0 and copies the first 19 chars; accepts optional fraction and +00:00 / Z. */
static int parse_time(const uint8_t *s, uint32_t n, char out[20]) {
    if (n < 20u) return -1;
    for (uint32_t i = 0; i < 19u; ++i) {
        uint8_t c = s[i];
        char sep = (i == 4 || i == 7) ? '-' : (i == 10) ? 'T' : (i == 13 || i == 16) ? ':' : 0;
        if (sep ? (c != (uint8_t)sep) : (c < '0' || c > '9')) return -1;
        out[i] = (char)c;
    }
    out[19] = 0;
    uint32_t i = 19u;
    if (s[i] == '.') { ++i; uint32_t d = 0; while (i < n && s[i] >= '0' && s[i] <= '9') { ++i; ++d; } if (d == 0) return -1; }
    if (i + 1u == n && s[i] == 'Z') return 0;
    if (i + 6u == n && s[i] == '+' && s[i+1] == '0' && s[i+2] == '0' && s[i+3] == ':' && s[i+4] == '0' && s[i+5] == '0') return 0;
    return -1;                                               /* any other offset: fail closed */
}
static int time_valid(const char *s) {                       /* calendar sanity for "now" */
    for (int i = 0; i < 19; ++i) { char c = s[i]; int sep = (i == 4 || i == 7 || i == 10 || i == 13 || i == 16); if (sep ? 0 : (c < '0' || c > '9')) return 0; }
    return s[4] == '-' && s[7] == '-' && s[10] == 'T' && s[13] == ':' && s[16] == ':' && s[19] == 0;
}
static int time_cmp(const char *a, const char *b) { for (int i = 0; i < 19; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }

int sand_stci_verify_sapp_v2_ex(const void *image_v, uint32_t image_size,
                                const struct sand_stci_trust_store *ts, const struct sand_stci_policy *pol,
                                uint8_t digest_out[SAND_STCI_SHA256_SIZE], struct sand_stci_result *result) {
    const uint8_t *img = (const uint8_t *)image_v;
    if (!img || !ts || !pol) return SAND_STCI_ERR_ARGUMENT;
    if (pol->abi_major != SAND_STCI_ABI_MAJOR || pol->struct_size < sizeof(*pol) ||
        ts->abi_major != SAND_STCI_ABI_MAJOR || ts->struct_size < sizeof(*ts)) return SAND_STCI_ERR_ARGUMENT;
    if (pol->flags & SAND_STCI_POLICY_REQUIRE_ATTESTATION) return SAND_STCI_ERR_UNSUPPORTED;   /* no attestation service yet */
    if (ts->root_count > SAND_STCI_MAX_ROOTS || (ts->root_count && !ts->roots) || (ts->revoked_count && !ts->revoked_key_ids)) return SAND_STCI_ERR_ARGUMENT;
    if (image_size < SAPP_HEADER_SIZE || image_size > SAND_STCI_MAX_IMAGE_SIZE) return SAND_STCI_ERR_FORMAT;

    /* 1. header and layout */
    if (img[0] != 'S' || img[1] != 'A' || img[2] != 'P' || img[3] != 'P') return SAND_STCI_ERR_FORMAT;
    if (rd16(img + 4) != 2u) return SAND_STCI_ERR_UNSUPPORTED;                     /* v1 is never "signed" */
    if (rd16(img + 6) != 1u || rd32(img + 8) != SAPP_HEADER_SIZE || rd32(img + 12) != 0u || rd32(img + 36) != image_size) return SAND_STCI_ERR_FORMAT;
    for (int i = 56; i < 64; ++i) if (img[i]) return SAND_STCI_ERR_FORMAT;
    uint32_t seg_off = rd32(img + 20), seg_cnt = rd32(img + 24), meta_off = rd32(img + 28), meta_len = rd32(img + 32);
    uint32_t sig_off = rd32(img + 40), sig_len = rd32(img + 44), rel_off = rd32(img + 48), rel_cnt = rd32(img + 52);
    if (seg_cnt == 0u || seg_cnt > 8u || meta_len == 0u || meta_len > 4096u || rel_cnt > 256u) return SAND_STCI_ERR_FORMAT;
    if (sig_off == 0u || sig_len == 0u || sig_len > SIG_BLOCK_MAX || !in_range(sig_off, sig_len, image_size) || sig_off + sig_len != image_size) return SAND_STCI_ERR_FORMAT;
    uint32_t covered_end = seg_off + seg_cnt * SAPP_SEGMENT_SIZE;
    if (seg_off < SAPP_HEADER_SIZE || !in_range(seg_off, seg_cnt * SAPP_SEGMENT_SIZE, sig_off)) return SAND_STCI_ERR_FORMAT;
    if (meta_off < covered_end || !in_range(meta_off, meta_len, sig_off)) return SAND_STCI_ERR_FORMAT;
    covered_end = meta_off + meta_len;
    if (rel_cnt) { if (rel_off < covered_end || !in_range(rel_off, rel_cnt * SAPP_RELOC_SIZE, sig_off)) return SAND_STCI_ERR_FORMAT; covered_end = rel_off + rel_cnt * SAPP_RELOC_SIZE; }
    else if (rel_off != 0u) return SAND_STCI_ERR_FORMAT;
    for (uint32_t s = 0; s < seg_cnt; ++s) {                                        /* payloads must precede the signature block */
        const uint8_t *sg = img + seg_off + s * SAPP_SEGMENT_SIZE;
        uint32_t fo = rd32(sg + 8), fs = rd32(sg + 12);
        if (fs && (fo < covered_end || !in_range(fo, fs, sig_off))) return SAND_STCI_ERR_FORMAT;
    }

    /* 2. signature block */
    const uint8_t *blk = img + sig_off; struct span v;
    if (obj_find(blk, sig_len, "format", &v, 0) != 0 || !str_eq(blk, v, "SAND-SAPP-SIGNATURE")) return SAND_STCI_ERR_FORMAT;
    if (obj_find(blk, sig_len, "version", &v, 0) != 0 || !int_eq(blk, v, 1u)) return SAND_STCI_ERR_UNSUPPORTED;
    if (obj_find(blk, sig_len, "algorithm", &v, 0) != 0 || !str_eq(blk, v, "Ed25519")) return SAND_STCI_ERR_UNSUPPORTED;
    uint8_t sig[64];
    { const uint8_t *s; uint32_t n;
      if (obj_find(blk, sig_len, "signature", &v, 0) != 0 || str_body(blk, v, &s, &n) != 0 || n != 88u || b64_decode(s, n, sig, 64u) != 0) return SAND_STCI_ERR_SIGNATURE; }
    struct span cv;
    if (obj_find(blk, sig_len, "publisherCertificate", &cv, 0) != 0) return SAND_STCI_ERR_CERTIFICATE;
    const uint8_t *cert = blk + cv.start; uint32_t cert_len = cv.end - cv.start;

    /* 3. publisher certificate (canonical, so the issuer-signed body is the text minus issuerSignature) */
    if (obj_find(cert, cert_len, "format", &v, 0) != 0 || !str_eq(cert, v, "SAND-PUBLISHER-CERT")) return SAND_STCI_ERR_CERTIFICATE;
    if (obj_find(cert, cert_len, "version", &v, 0) != 0 || !int_eq(cert, v, 1u)) return SAND_STCI_ERR_UNSUPPORTED;
    { struct span ku, mm;
      if (obj_find(cert, cert_len, "keyUsage", &ku, &mm) != 0) return SAND_STCI_ERR_CERTIFICATE;
      static const char want[] = "[\"codeSigning\"]"; uint32_t wl = sizeof(want) - 1u;
      if (ku.end - ku.start != wl) return SAND_STCI_ERR_POLICY;
      for (uint32_t i = 0; i < wl; ++i) if (cert[ku.start + i] != (uint8_t)want[i]) return SAND_STCI_ERR_POLICY; }
    uint8_t pub_key[32], pub_id[32];
    { const uint8_t *s; uint32_t n;
      if (obj_find(cert, cert_len, "publicKeyPem", &v, 0) != 0 || str_body(cert, v, &s, &n) != 0 || parse_pem_spki(s, n, pub_key, pub_id) != 0) return SAND_STCI_ERR_CERTIFICATE;
      char hex[65]; hex_of(pub_id, hex);
      if (obj_find(cert, cert_len, "keyId", &v, 0) != 0 || !str_eq(cert, v, hex)) return SAND_STCI_ERR_CERTIFICATE; }
    uint8_t iss_sig[64];
    struct span msig;
    { const uint8_t *s; uint32_t n;
      if (obj_find(cert, cert_len, "issuerSignature", &v, &msig) != 0 || str_body(cert, v, &s, &n) != 0 || n != 88u || b64_decode(s, n, iss_sig, 64u) != 0) return SAND_STCI_ERR_CERTIFICATE; }

    /* 4. validity window (fail closed without a clock) */
    char nb[20], na[20];
    { const uint8_t *s; uint32_t n;
      if (obj_find(cert, cert_len, "notBefore", &v, 0) != 0 || str_body(cert, v, &s, &n) != 0 || parse_time(s, n, nb) != 0) return SAND_STCI_ERR_CERTIFICATE;
      if (obj_find(cert, cert_len, "notAfter", &v, 0) != 0 || str_body(cert, v, &s, &n) != 0 || parse_time(s, n, na) != 0) return SAND_STCI_ERR_CERTIFICATE; }
    if (!pol->now_utc || !time_valid(pol->now_utc)) return SAND_STCI_ERR_EXPIRED;
    if (time_cmp(pol->now_utc, nb) < 0 || time_cmp(pol->now_utc, na) > 0) return SAND_STCI_ERR_EXPIRED;

    /* 5. issuer must be a provisioned, non-revoked root; the package can never introduce one */
    const struct sand_stci_root *root = 0;
    { const uint8_t *s; uint32_t n;
      if (obj_find(cert, cert_len, "issuerKeyId", &v, 0) != 0 || str_body(cert, v, &s, &n) != 0 || n != 64u) return SAND_STCI_ERR_CERTIFICATE;
      for (uint32_t r = 0; r < ts->root_count; ++r) { char hex[65]; hex_of(ts->roots[r].key_id, hex); if (str_eq(cert, v, hex)) { root = &ts->roots[r]; break; } } }
    if (!root) return SAND_STCI_ERR_UNTRUSTED_ROOT;
    if (root->flags & SAND_STCI_ROOT_REVOKED) return SAND_STCI_ERR_REVOKED;
    for (uint32_t r = 0; r < ts->revoked_count; ++r) if (mem_eq(ts->revoked_key_ids[r], pub_id, 32u)) return SAND_STCI_ERR_REVOKED;

    /* 6. root's signature over the canonical certificate body (certificate minus issuerSignature, one comma) */
    { uint32_t cut_s = msig.start - 1u;                      /* the comma before the member */
      if (cert[cut_s] != ',' ) return SAND_STCI_ERR_CERTIFICATE;
      if (!sand_ed25519_verify2(iss_sig, root->public_key, cert, cut_s, cert + msig.end, cert_len - msig.end)) return SAND_STCI_ERR_CERTIFICATE; }

    /* 7. publisher's signature over domain || file[0, signature_offset) */
    if (!sand_ed25519_verify2(sig, pub_key, SAND_STCI_SAPP_V2_DOMAIN, SAND_STCI_SAPP_V2_DOMAIN_SIZE, img, sig_off)) return SAND_STCI_ERR_SIGNATURE;

    if (digest_out) sand_sha256(img, image_size, digest_out);
    if (result) { for (int i = 0; i < 32; ++i) { result->publisher_key_id[i] = pub_id[i]; result->root_key_id[i] = root->key_id[i]; } }
    return SAND_STCI_OK;
}

int sand_stci_verify_sapp_v2(const void *image, uint32_t image_size, const struct sand_stci_trust_store *trust_store,
                             const struct sand_stci_policy *policy, uint8_t digest_out[SAND_STCI_SHA256_SIZE]) {
    return sand_stci_verify_sapp_v2_ex(image, image_size, trust_store, policy, digest_out, 0);
}
