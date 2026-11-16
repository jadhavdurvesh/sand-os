#include "citadel/citadel.h"
#include <string.h>

static int valid_text_id(const char *value) {
    size_t i;
    if (value == NULL || value[0] == '\0') return 0;
    for (i = 0; i < CITADEL_ID_LENGTH; ++i) {
        if (value[i] == '\0') return 1;
    }
    return 0;
}

CitadelStatus citadel_registry_init(CitadelRegistry *registry) {
    if (registry == NULL) return CITADEL_ERR_INVALID_ARGUMENT;
    memset(registry, 0, sizeof(*registry));
    return CITADEL_OK;
}

CitadelStatus citadel_register_capability(
    CitadelRegistry *registry, const CitadelCapability *descriptor,
    CitadelCapabilityFn execute, void *user_data) {
    uint32_t i;
    if (registry == NULL || descriptor == NULL || execute == NULL ||
        descriptor->id[0] == '\0' ||
        memchr(descriptor->id, 0, sizeof(descriptor->id)) == NULL) {
        return CITADEL_ERR_INVALID_ARGUMENT;
    }
    if (registry->capability_count >= CITADEL_MAX_CAPABILITIES)
        return CITADEL_ERR_LIMIT;
    for (i = 0; i < registry->capability_count; ++i) {
        if (strcmp(registry->capabilities[i].descriptor.id, descriptor->id) == 0)
            return CITADEL_ERR_INVALID_ARGUMENT;
    }
    registry->capabilities[registry->capability_count].descriptor = *descriptor;
    registry->capabilities[registry->capability_count].execute = execute;
    registry->capabilities[registry->capability_count].user_data = user_data;
    registry->capability_count++;
    return CITADEL_OK;
}

CitadelStatus citadel_register_intent(
    CitadelRegistry *registry, const char *intent_id,
    const char *phrase, const char *capability_id) {
    uint32_t i;
    size_t length = 0;
    if (registry == NULL || !valid_text_id(intent_id) ||
        !valid_text_id(capability_id) || phrase == NULL) {
        return CITADEL_ERR_INVALID_ARGUMENT;
    }
    while (length < CITADEL_MAX_TOKEN_LENGTH && phrase[length] != '\0') length++;
    if (length == 0 || length >= CITADEL_MAX_TOKEN_LENGTH) return CITADEL_ERR_LIMIT;
    {   /* '*' is allowed only as a standalone, final token, and never as the whole phrase */
        size_t i, token_start = 0, tokens = 0;
        for (i = 0; i <= length; ++i) {
            if (i == length || phrase[i] == ' ') {
                if (i > token_start) {
                    ++tokens;
                    if (i - token_start == 1 && phrase[token_start] == '*') {
                        size_t j; for (j = i; j < length; ++j) if (phrase[j] != ' ') return CITADEL_ERR_INVALID_ARGUMENT;
                        if (tokens == 1) return CITADEL_ERR_INVALID_ARGUMENT;
                    } else {
                        size_t k; for (k = token_start; k < i; ++k) if (phrase[k] == '*') return CITADEL_ERR_INVALID_ARGUMENT;
                    }
                }
                token_start = i + 1;
            }
        }
    }
    if (registry->intent_count >= CITADEL_MAX_INTENTS) return CITADEL_ERR_LIMIT;
    for (i = 0; i < registry->intent_count; ++i) {
        if (strcmp(registry->intents[i].phrase, phrase) == 0 ||
            strcmp(registry->intents[i].id, intent_id) == 0)
            return CITADEL_ERR_INVALID_ARGUMENT;
    }
    memset(&registry->intents[registry->intent_count], 0, sizeof(CitadelIntent));
    memcpy(registry->intents[registry->intent_count].id, intent_id, strlen(intent_id) + 1);
    memcpy(registry->intents[registry->intent_count].phrase, phrase, length + 1);
    memcpy(registry->intents[registry->intent_count].capability_id, capability_id,
           strlen(capability_id) + 1);
    registry->intent_count++;
    return CITADEL_OK;
}
