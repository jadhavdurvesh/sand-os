#include "citadel/citadel.h"
#include <string.h>

static int matches(const CitadelCommand *command, const char *phrase) {
    size_t offset = 0;
    uint32_t token_index = 0;
    size_t phrase_length = strlen(phrase);
    while (offset < phrase_length) {
        size_t start;
        size_t length;
        while (offset < phrase_length && phrase[offset] == ' ') offset++;
        if (offset == phrase_length) break;
        start = offset;
        while (offset < phrase_length && phrase[offset] != ' ') offset++;
        length = offset - start;
        if (length == 1 && phrase[start] == '*') {
            /* Trailing wildcard (registry guarantees it is the last token): one or more arguments. */
            return token_index < command->token_count && token_index > 0;
        }
        if (token_index >= command->token_count ||
            command->tokens[token_index].length != length ||
            memcmp(command->tokens[token_index].data, phrase + start, length) != 0)
            return 0;
        token_index++;
    }
    return token_index == command->token_count && token_index > 0;
}

CitadelStatus citadel_dispatch(
    const CitadelRegistry *registry, const CitadelCommand *command,
    const CitadelPolicy *policy, CitadelResult *result) {
    uint32_t i, j;
    if (registry == NULL || command == NULL || policy == NULL || result == NULL)
        return CITADEL_ERR_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));
    for (i = 0; i < registry->intent_count; ++i) {
        if (matches(command, registry->intents[i].phrase)) {
            for (j = 0; j < registry->capability_count; ++j) {
                const CitadelCapability *capability =
                    &registry->capabilities[j].descriptor;
                if (strcmp(capability->id, registry->intents[i].capability_id) == 0) {
                    if ((capability->required_permissions & policy->granted_permissions) !=
                            capability->required_permissions ||
                        (capability->requires_confirmation && !policy->confirmation_granted)) {
                        result->status = CITADEL_ERR_DENIED;
                        return result->status;
                    }
                    result->status = registry->capabilities[j].execute(
                        command, result, registry->capabilities[j].user_data);
                    return result->status;
                }
            }
            result->status = CITADEL_ERR_UNAVAILABLE;
            return result->status;
        }
    }
    result->status = CITADEL_ERR_NOT_FOUND;
    return result->status;
}

int citadel_requires_confirmation(const CitadelRegistry *registry, const CitadelCommand *command) {
    uint32_t i, j;
    if (registry == NULL || command == NULL) return 0;
    for (i = 0; i < registry->intent_count; ++i) {
        if (!matches(command, registry->intents[i].phrase)) continue;
        for (j = 0; j < registry->capability_count; ++j)
            if (strcmp(registry->capabilities[j].descriptor.id, registry->intents[i].capability_id) == 0)
                return registry->capabilities[j].descriptor.requires_confirmation != 0;
        return 0;
    }
    return 0;
}
