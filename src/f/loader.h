#ifndef f_loader_h
#define f_loader_h

#include "../common.h"

#define HEADER_SIZE 0x10

typedef struct Driver Driver;

typedef struct FCartInfo {
    blob prg_rom;
    blob chr_rom;
    bool has_battery_backup;
    int default_mirroring;
    int mapper_id;
} FCartInfo;

int ines_loader(Driver *driver, blob *rom);

void f_teardown(Driver *driver);

#endif /* f_loader_h */
