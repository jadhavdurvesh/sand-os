#include "citadel/citadel.h"
#include <ctype.h>
#include <string.h>
CitadelStatus citadel_parse(const char *input, size_t n, CitadelCommand *out) {
 size_t i=0;
 if (!input || !out) return CITADEL_ERR_INVALID_ARGUMENT;
 if (!n || n>CITADEL_MAX_INPUT) return CITADEL_ERR_LIMIT;
 memset(out,0,sizeof(*out));
 while (i<n) {
  size_t start,len;
  while (i<n && isspace((unsigned char)input[i])) i++;
  if (i==n) break;
  if (out->token_count>=CITADEL_MAX_TOKENS) return CITADEL_ERR_LIMIT;
  start=i; while (i<n && !isspace((unsigned char)input[i])) i++;
  len=i-start;
  if (!len || len>=CITADEL_MAX_TOKEN_LENGTH) return CITADEL_ERR_LIMIT;
  out->tokens[out->token_count].data=input+start;
  out->tokens[out->token_count].length=len;
  out->token_count++;
 }
 return out->token_count ? CITADEL_OK : CITADEL_ERR_INVALID_ARGUMENT;
}
