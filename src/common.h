#ifndef common_h
#define common_h

#ifdef _WIN32
#define __USE_MINGW_ANSI_STDIO 1
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_NAME "f-type"
#define APP_HOMEPAGE "https://github.com/sroby/f-type"

#ifndef BUILD_ID
#define BUILD_ID "[unknown]"
#endif

typedef struct {
    uint8_t *data;
    size_t size;
} blob;

#define BIT_SET(x, n)       (x) |= (    1 << (n))
#define BIT_SET_IF(x, n, v) (x) |= (!!(v) << (n))

#define BIT_CLEAR(x, n)       (x) &= ~(    1 << (n))
#define BIT_CLEAR_IF(x, n, v) (x) &= ~(!!(v) << (n))

#define BIT_TOGGLE(x, n)       (x) ^= (    1 << (n))
#define BIT_TOGGLE_IF(x, n, v) (x) ^= (!!(v) << (n))

#define BIT_AS(x, n, v) (x) = ((x) | (-(!!(v)) & (1 << (n)))) \
                            & ~(-(!(v)) & (1 << (n)))

#define BIT_CHECK(x, n) ((x) & (1 << (n)))

#endif /* common_h */
