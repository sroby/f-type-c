#ifndef common_h
#define common_h

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

#endif /* common_h */
