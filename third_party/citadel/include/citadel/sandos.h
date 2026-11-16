#ifndef CITADEL_SANDOS_H
#define CITADEL_SANDOS_H
#include <stddef.h>
#include <stdint.h>
#include "citadel.h"
#ifdef __cplusplus
extern "C" {
#endif
/*
 * SandOS adapter. The adapter holds all assistant behaviour (what "open locus" or "set accent jade" mean, what
 * needs confirmation, how answers read) and none of the OS calls: the host supplies a platform table, which on
 * SAND OS maps to SAPP syscalls (apps/citadel in the SAND OS repository) and in tests to a fake.
 * It needs no libc beyond memcpy/memset/strlen/strcmp, so it links into a freestanding SAPP.
 */
typedef struct CitadelSandTime { uint32_t year, month, day, hour, minute, second; } CitadelSandTime;
typedef struct CitadelSandInfo { uint32_t uptime_seconds, free_kib, total_kib, process_count; } CitadelSandInfo;

#define CITADEL_SAND_SETTING_ACCENT 1u
#define CITADEL_SAND_SETTING_REDUCED_MOTION 2u
#define CITADEL_SAND_SETTING_CLOCK_24H 3u
#define CITADEL_SAND_ACCENT_COUNT 5u

typedef struct CitadelSandPlatform {
    void *context;
    uint32_t (*app_count)(void *context);                                          /* launchable apps */
    int (*app_name)(void *context, uint32_t index, char *out, size_t capacity);    /* 0 ok */
    int (*launch)(void *context, const char *app);                                 /* 0 ok, <0 refused/unknown */
    int (*setting_get)(void *context, uint32_t key);                               /* >=0 value, <0 error */
    int (*setting_set)(void *context, uint32_t key, uint32_t value);               /* 0 ok */
    int (*wall_time)(void *context, CitadelSandTime *out);                         /* 0 ok; may be NULL */
    int (*system_info)(void *context, CitadelSandInfo *out);                       /* 0 ok; may be NULL */
} CitadelSandPlatform;

/* Registers capabilities and intents. The platform table must outlive the registry. */
CitadelStatus citadel_sandos_register(CitadelRegistry *registry, const CitadelSandPlatform *platform);
/* Policy the SAND OS assistant app grants (read system, launch apps, change settings; confirmation per command). */
#define CITADEL_SAND_POLICY_PERMISSIONS (CITADEL_PERMISSION_READ_SYSTEM | CITADEL_PERMISSION_LAUNCH_APP | CITADEL_PERMISSION_MODIFY_SETTINGS)
#ifdef __cplusplus
}
#endif
#endif
