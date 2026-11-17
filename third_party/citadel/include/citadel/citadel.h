#ifndef CITADEL_CITADEL_H
#define CITADEL_CITADEL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CITADEL_ABI_VERSION 1u
#define CITADEL_MAX_INPUT 256u
#define CITADEL_MAX_TOKENS 16u
#define CITADEL_MAX_TOKEN_LENGTH 48u
#define CITADEL_MAX_INTENTS 32u
#define CITADEL_MAX_CAPABILITIES 32u
#define CITADEL_MAX_OUTPUT 256u
#define CITADEL_ID_LENGTH 32u
typedef enum CitadelStatus {
 CITADEL_OK=0, CITADEL_ERR_INVALID_ARGUMENT=-1, CITADEL_ERR_LIMIT=-2,
 CITADEL_ERR_NOT_FOUND=-3, CITADEL_ERR_DENIED=-4, CITADEL_ERR_UNAVAILABLE=-5,
 CITADEL_ERR_BUFFER_TOO_SMALL=-6, CITADEL_ERR_ABI_MISMATCH=-7, CITADEL_ERR_INTERNAL=-8
} CitadelStatus;
typedef struct CitadelSlice { const char *data; size_t length; } CitadelSlice;
typedef struct CitadelCommand { uint32_t token_count; CitadelSlice tokens[CITADEL_MAX_TOKENS]; } CitadelCommand;
typedef struct CitadelResult { CitadelStatus status; char text[CITADEL_MAX_OUTPUT]; } CitadelResult;
typedef enum CitadelPermission {
 CITADEL_PERMISSION_NONE=0u, CITADEL_PERMISSION_READ_SYSTEM=1u<<0,
 CITADEL_PERMISSION_LAUNCH_APP=1u<<1, CITADEL_PERMISSION_MODIFY_SETTINGS=1u<<2,
 CITADEL_PERMISSION_FILE_ACCESS=1u<<3, CITADEL_PERMISSION_NETWORK=1u<<4,
 CITADEL_PERMISSION_PRIVILEGED=1u<<5, CITADEL_PERMISSION_SCHEDULE=1u<<6
} CitadelPermission;
typedef struct CitadelIntent { char id[CITADEL_ID_LENGTH]; char phrase[CITADEL_MAX_TOKEN_LENGTH]; char capability_id[CITADEL_ID_LENGTH]; } CitadelIntent;
typedef struct CitadelCapability { char id[CITADEL_ID_LENGTH]; uint32_t required_permissions; uint8_t requires_confirmation; } CitadelCapability;
typedef struct CitadelPolicy { uint32_t granted_permissions; uint8_t confirmation_granted; } CitadelPolicy;
typedef CitadelStatus (*CitadelCapabilityFn)(const CitadelCommand *, CitadelResult *, void *);
typedef struct CitadelCapabilityEntry { CitadelCapability descriptor; CitadelCapabilityFn execute; void *user_data; } CitadelCapabilityEntry;
typedef struct CitadelRegistry {
 uint32_t intent_count; CitadelIntent intents[CITADEL_MAX_INTENTS];
 uint32_t capability_count; CitadelCapabilityEntry capabilities[CITADEL_MAX_CAPABILITIES];
} CitadelRegistry;
CitadelStatus citadel_parse(const char *, size_t, CitadelCommand *);
CitadelStatus citadel_registry_init(CitadelRegistry *);
CitadelStatus citadel_register_capability(CitadelRegistry *, const CitadelCapability *, CitadelCapabilityFn, void *);
CitadelStatus citadel_register_intent(CitadelRegistry *, const char *, const char *, const char *);
/* 1 when the command matches an intent whose capability requires confirmation (nothing is executed), else 0. */
int citadel_requires_confirmation(const CitadelRegistry *, const CitadelCommand *);
CitadelStatus citadel_dispatch(const CitadelRegistry *, const CitadelCommand *, const CitadelPolicy *, CitadelResult *);
#ifdef __cplusplus
}
#endif
#endif
