#ifndef s_loader_h
#define s_loader_h

#include "../common.h"

#define HEADER_EXT_MAKER_CODE 0x00
#define HEADER_EXT_GAME_CODE 0x02
#define HEADER_EXT_RAM_SIZE 0x0D
#define HEADER_EXT_SPECIAL 0x0E
#define HEADER_CART_TYPE_SUB 0x0F
#define HEADER_GAME_TITLE 0x10
#define HEADER_MAP_MODE 0x25
#define HEADER_CART_TYPE 0x26
#define HEADER_ROM_SIZE 0x27
#define HEADER_RAM_SIZE 0x28
#define HEADER_DEST_CODE 0x29
#define HEADER_OLD_MAKER_CODE 0x2A
#define HEADER_MASK_ROM_VERSION 0x2B
#define HEADER_COMPLEMENT 0x2C
#define HEADER_CHECKSUM 0x2E

#define STR_NOT_IN_HEADER "[not present in header]"

typedef struct Driver Driver;

typedef enum ExChipType {
    EXCHIP_NONE = 0,
    EXCHIP_DSP,
    EXCHIP_GSU,
    EXCHIP_OBC1,
    EXCHIP_SA_1,
    EXCHIP_S_DD1,
    EXCHIP_S_RTC,
    EXCHIP_OTHER,
    EXCHIP_SPC7110,
    EXCHIP_ST01X,
    EXCHIP_ST018,
    EXCHIP_CX4,
} ExChipType;

typedef struct CartInfo {
    // ROM
    uint8_t *rom;
    int rom_size;
    bool has_fast_rom;
    
    // RAM
    int ram_size;
    int exp_ram_size;
    bool has_battery_backup;
    
    // Mapper
    int map_mode;
    ExChipType ex_chip;
} CartInfo;

int s_loader(Driver *driver, uint8_t *rom_data, int rom_data_size);

#endif /* s_loader_h */
