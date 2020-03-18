#include "loader.h"

#include <ctype.h>

#include "../driver.h"

const char *map_mode_names[] = {
    "aka. \"LoROM\"",
    "aka. \"HiROM\"",
    "SDD-1 Super MMC",
    "SA-1 Super MMC",
    "",
    "aka. \"ExHiROM\"",
    "", "", "", "",
    "SPC7110"
};

const char *chip_names[] = {
    "None",
    "DSP",
    "GSU (aka. SuperFX)",
    "OBC1",
    "SA-1",
    "S-DD1",
    "S-RTC",
    "Other",
    "SPC7110",
    "ST010/ST011",
    "ST018",
    "CX4"
};

const char *dest_codes = "JEPW??FHSDIC?KANBUXYZ";

int s_loader(Driver *driver, uint8_t *rom_data, int rom_data_size) {
    // Round upwards to the nearest kilobyte,
    // to get rid of possible copier headers
    rom_data += rom_data_size % 1024;
    
    // Look for SFC header
    const int header_offsets[] = {0x7FB0, 0xFFB0, 0x40FFB0};
    const char valid_map_modes[][3] = {
        {0x20, 0x22, 0x23},
        {0x21, 0x2a, 0x00},
        {0x25, 0x00, 0x00},
    };
    bool valid = false;
    uint8_t *header = rom_data;
    char title[22];
    int header_pos = 0;
    CartInfo cart;
    for (int i = 0; i < sizeof(header_offsets) / sizeof(int); i++) {
        header_pos = header_offsets[i];
        if (rom_data_size < (header_pos + 0x50)) {
            break;
        }
        header = rom_data + header_pos;
        
        cart.map_mode = header[HEADER_MAP_MODE] & ~0b10000;
        for (int j = 0; j < sizeof(valid_map_modes[i]); j++) {
            if (!valid_map_modes[i][j]) {
                break;
            }
            if (cart.map_mode == valid_map_modes[i][j]) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            continue;
        }
        
        strncpy(title, (const char *)header + HEADER_GAME_TITLE,
                sizeof(title) - 1);
        title[sizeof(title) - 1] = 0;
        long title_len = strlen(title);
        if (title_len < 20) {
            valid = false;
        } else {
            for (int j = 0; j < title_len; j++) {
                // Validate that the title is a valid text string
                if (iscntrl(title[j])) {
                    valid = false;
                    break;
                }
                // Take out non-ASCII (Shift-JIS) characters
                // TODO: use iconv to convert to UTF-8
                if (title[j] < 0) {
                    title[j] = ' ';
                }
            }
        }
        if (!valid) {
            continue;
        }
        
        // Parse cartridge type
        switch (header[HEADER_CART_TYPE] & 0xF) {
            // No SRAM, no battery
            case 0x0:
            case 0x1:
            case 0x3:
            case 0x4:
                cart.has_battery_backup = false;
                break;
            // SRAM, battery
            case 0x2:
            case 0x5:
            case 0x6:
            case 0x9:
            case 0xA:
                cart.has_battery_backup = true;
                break;
            default:
                valid = false;
        }
        switch (header[HEADER_CART_TYPE] >> 4) {
            case 0x0:
                cart.ex_chip = ((header[HEADER_CART_TYPE] & 0xF) >= 3 ?
                                  EXCHIP_DSP : EXCHIP_NONE);
                break;
            case 0x1:
                cart.ex_chip = EXCHIP_GSU;
                break;
            case 0x2:
                cart.ex_chip = EXCHIP_OBC1;
                break;
            case 0x3:
                cart.ex_chip = EXCHIP_SA_1;
                break;
            case 0x4:
                cart.ex_chip = EXCHIP_S_DD1;
                break;
            case 0x5:
                cart.ex_chip = EXCHIP_S_RTC;
                break;
            case 0xE:
                cart.ex_chip = EXCHIP_OTHER;
                break;
            case 0xF:
                switch (header[HEADER_CART_TYPE_SUB]) {
                    case 0x00:
                        cart.ex_chip = EXCHIP_SPC7110;
                        break;
                    case 0x01:
                        cart.ex_chip = EXCHIP_ST01X;
                        break;
                    case 0x02:
                        cart.ex_chip = EXCHIP_ST018;
                        break;
                    case 0x10:
                        cart.ex_chip = EXCHIP_CX4;
                        break;
                    default:
                        valid = false;
                }
                break;
            default:
                valid = false;
        }
        if (valid) {
            break;
        }
    }
    if (!valid) {
        fprintf(stderr, "Could not identify file type\n");
        return 1;
    }
    
    cart.rom = rom_data;
    cart.rom_size = rom_data_size;
    
    fprintf(stderr, "Raw SHVC ROM image (header found at 0x%06X)\n",
            header_pos);
    fprintf(stderr, "Game title: %s\n", title);
    
    const char *code_display = STR_NOT_IN_HEADER;
    char code[5];
    bool has_ext_header = (header[HEADER_OLD_MAKER_CODE] == 0x33);
    if (has_ext_header) {
        memcpy(code, header + HEADER_EXT_GAME_CODE, 4);
        code[4] = 0;
        code_display = code;
    }
    fprintf(stderr, "Game code: %s\n", code_display);
    
    fprintf(stderr, "Maker code: ");
    if (has_ext_header) {
        char mcode[3];
        memcpy(mcode, header + HEADER_EXT_MAKER_CODE, 2);
        mcode[2] = 0;
        fprintf(stderr, "%s\n", mcode);
    } else {
        fprintf(stderr, "%02X\n", header[HEADER_OLD_MAKER_CODE]);
    }
    
    fprintf(stderr, "Map mode: %X (%s)\n",
            cart.map_mode, map_mode_names[cart.map_mode & 0xF]);
    cart.has_fast_rom = header[HEADER_MAP_MODE] & 0x10;
    fprintf(stderr, "ROM speed: %sns\n", (cart.has_fast_rom ? "120" : "200"));
    fprintf(stderr, "Co-processor: %s\n", chip_names[cart.ex_chip]);
    
    int reported_rom_size = 1 << header[HEADER_ROM_SIZE];
    int actual_rom_size = cart.rom_size >> 10;
    fprintf(stderr, "ROM size: %dKB", reported_rom_size);
    if (reported_rom_size != actual_rom_size) {
        fprintf(stderr, " in header, %dKB actual", actual_rom_size);
    }
    fprintf(stderr, "\n");
    if (reported_rom_size < actual_rom_size) {
        fprintf(stderr, "File size is smaller than expected\n");
        return 1;
    }
    
    int ram_size = (header[HEADER_RAM_SIZE] ? 1 << header[HEADER_RAM_SIZE] : 0);
    cart.ram_size = ram_size << 10;
    int exp_ram_size = 0;
    if (has_ext_header && header[HEADER_EXT_RAM_SIZE]) {
        exp_ram_size = 1 << header[HEADER_EXT_RAM_SIZE];
    }
    cart.exp_ram_size = exp_ram_size << 10;
    fprintf(stderr, "RAM size: %dKB + %dKB\n", ram_size, exp_ram_size);

    fprintf(stderr, "Battery-backed RAM: %s\n",
           (cart.has_battery_backup ? "Yes" : "No"));
    
    fprintf(stderr, "Destination code: ");
    if (header[HEADER_DEST_CODE] < sizeof(dest_codes)) {
        fprintf(stderr, "%c\n", dest_codes[header[HEADER_DEST_CODE]]);
    } else {
        fprintf(stderr, "(%d?)\n", header[HEADER_DEST_CODE]);
    }
    
    fprintf(stderr, "Mask ROM version: %d\n",
            header[HEADER_MASK_ROM_VERSION]);
    fprintf(stderr, "Special version: ");
    if (has_ext_header) {
        fprintf(stderr, "%d\n", header[HEADER_EXT_SPECIAL]);
    } else {
        fprintf(stderr, "%s\n", STR_NOT_IN_HEADER);
    }
    
    int complement = header[HEADER_COMPLEMENT] |
                     (header[HEADER_COMPLEMENT + 1] << 8);
    fprintf(stderr, "Complement check: 0x%04X\n", complement);
    int checksum = header[HEADER_CHECKSUM] |
                   (header[HEADER_CHECKSUM + 1] << 8);
    fprintf(stderr, "Checksum: 0x%04X\n", checksum);
    
    return 1;
}

/*
 if (ram_size) {
     cart.ram = malloc(cart.ram_size);
 }
 if (exp_ram_size) {
     cart.exp_ram = malloc(cart.exp_ram_size);
 }
 free(s_cart.exp_ram);
 free(s_cart.ram);
 */
