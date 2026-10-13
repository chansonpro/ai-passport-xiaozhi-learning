#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *title;
    const char *author;
    const char *text;
} tangshi_poem_t;

extern const tangshi_poem_t g_tangshi_poems[];
extern const size_t g_tangshi_poem_count;

#ifdef __cplusplus
}
#endif
