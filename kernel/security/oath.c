#include "sand/security/oath.h"
#include "sand/security/launch_gate.h"
#include "sand/fs/mimir.h"
#include "sand/crypto/ed25519.h"

#define OATH_FILE "boot.oath"
#define OATH_MAX_BYTES 2048u
#define OATH_MAX_REVOKED 8u

static struct sand_stci_root roots[SAND_STCI_MAX_ROOTS];
static uint8_t revoked[OATH_MAX_REVOKED][SAND_STCI_SHA256_SIZE];
static struct sand_stci_trust_store store;
static struct sand_stci_policy policy;
static uint32_t state_value;

static int hex(const uint8_t *p, uint32_t count, uint8_t *out) {
    for(uint32_t i=0;i<count;++i) {
        uint32_t v=0u;
        for(uint32_t k=0;k<2u;++k) { uint8_t c=p[2u*i+k]; uint32_t d=c>='0'&&c<='9'?(uint32_t)(c-'0'):c>='a'&&c<='f'?(uint32_t)(c-'a'+10):16u; if(d>15u) return -1; v=v*16u+d; }
        out[i]=(uint8_t)v;
    }
    return 0;
}
static int line_is(const uint8_t *t,uint32_t len,const char *word) { uint32_t i=0u; while(word[i]) { if(i>=len || t[i]!=(uint8_t)word[i]) return 0; ++i; } return 1; }

/* Parse and verify; fills roots/revoked/policy on success. */
static int parse(const uint8_t *t, uint32_t len) {
    static const char head[]="MIMIR-OATH 1\n"; static const uint8_t domain[]="MIMIR-OATH-V1";
    const uint8_t *key=mimir_sigil_pubkey();
    uint32_t i=sizeof(head)-1u, flags=0u, nroots=0u, nrev=0u; int have_policy=0;
    if(key==0 || len<i || !line_is(t,len,head)) return -1;
    for(;;) {
        if(i>=len) return -1;
        uint32_t e=i; while(e<len && t[e]!='\n') ++e;
        if(e>=len) return -1;
        uint32_t n=e-i;
        if(n==4u+128u && line_is(t+i,n,"sig ")) {
            uint8_t sig[64];
            if(!have_policy || hex(t+i+4u,64u,sig)!=0 || e+1u!=len) return -1;
            if(sand_ed25519_verify2(sig,key,domain,sizeof(domain),t,i)!=1) return -1;
            break;
        } else if(line_is(t+i,n,"policy ")) {
            if(have_policy) return -1;
            if(n==7u+14u && line_is(t+i+7u,n-7u,"require-signed")) flags=SAND_STCI_POLICY_REQUIRE_SIGNED;
            else if(n==7u+9u && line_is(t+i+7u,n-7u,"developer")) flags=SAND_STCI_POLICY_DEVELOPER_MODE;
            else return -1;
            have_policy=1;
        } else if(line_is(t+i,n,"root ")) {
            if(n!=5u+64u+1u+64u || t[i+5u+64u]!=' ' || nroots>=SAND_STCI_MAX_ROOTS) return -1;
            if(hex(t+i+5u,32u,roots[nroots].key_id)!=0 || hex(t+i+5u+65u,32u,roots[nroots].public_key)!=0) return -1;
            roots[nroots].flags=0u; ++nroots;
        } else if(line_is(t+i,n,"revoke ")) {
            if(n!=7u+64u || nrev>=OATH_MAX_REVOKED || hex(t+i+7u,32u,revoked[nrev])!=0) return -1;
            ++nrev;
        } else return -1;
        i=e+1u;
    }
    policy.abi_major=SAND_STCI_ABI_MAJOR; policy.struct_size=sizeof(policy); policy.flags=flags; policy.reserved=0u; policy.now_utc=0;
    store.abi_major=SAND_STCI_ABI_MAJOR; store.struct_size=sizeof(store); store.root_count=nroots; store.generation=1u;
    store.roots=roots; store.revoked_key_ids=(const uint8_t (*)[SAND_STCI_SHA256_SIZE])revoked; store.revoked_count=nrev;
    return 0;
}
static void put_count(char *line, uint32_t *n, uint32_t v) { if(v>=10u) line[(*n)++]=(char)('0'+v/10u); line[(*n)++]=(char)('0'+v%10u); }

uint32_t sand_oath_load(void) {
    static uint8_t text[OATH_MAX_BYTES+1u]; uint32_t len=0u; struct mimir_info info;
    if(!mimir_is_mounted() || mimir_stat(OATH_FILE,&info)!=0) { state_value=SAND_OATH_ABSENT; sand_audit("OATH_ABSENT developer profile, no trust roots"); return state_value; }
    if(info.size>OATH_MAX_BYTES || mimir_read(OATH_FILE,text,OATH_MAX_BYTES,&len)!=0 || parse(text,len)!=0) {
        static const struct sand_stci_trust_store empty={SAND_STCI_ABI_MAJOR,sizeof(struct sand_stci_trust_store),0u,1u,0,0,0u};
        static const struct sand_stci_policy strict={SAND_STCI_ABI_MAJOR,sizeof(struct sand_stci_policy),SAND_STCI_POLICY_REQUIRE_SIGNED,0u,0};
        sand_launch_gate_configure(&strict,&empty);       /* fail closed: nothing signed is trusted, nothing unsigned runs */
        state_value=SAND_OATH_INVALID; sand_audit("OATH_INVALID fail closed: require-signed, no roots"); return state_value;
    }
    sand_launch_gate_configure(&policy,&store);
    { char line[64]="OATH_LOADED policy="; uint32_t n=19u; const char *w=(policy.flags&SAND_STCI_POLICY_REQUIRE_SIGNED)?"require-signed":"developer";
      for(uint32_t k=0u;w[k];++k) line[n++]=w[k];
      line[n++]=' '; line[n++]='r'; line[n++]='o'; line[n++]='o'; line[n++]='t'; line[n++]='s'; line[n++]='='; put_count(line,&n,store.root_count);
      line[n++]=' '; line[n++]='r'; line[n++]='e'; line[n++]='v'; line[n++]='='; put_count(line,&n,store.revoked_count); line[n]='\0'; sand_audit(line); }
    state_value=SAND_OATH_LOADED; return state_value;
}
uint32_t sand_oath_state(void) { return state_value; }
