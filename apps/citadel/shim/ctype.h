#ifndef CITADEL_SHIM_CTYPE_H
#define CITADEL_SHIM_CTYPE_H
static inline int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
#endif
