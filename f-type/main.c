#include "common.h"
#include <string.h>

#include "machine.h"
#include "memory_maps.h"

int main(int argc, const char *argv[]) {
    const char *const verb_char = getenv("VERBOSE");
    const bool verbose = verb_char ? *verb_char - '0' : false;
    
    if (argc < 2) {
        printf("Usage: %s rom.nes [debug.map]\n", argv[0]);
        return 1;
    }
    
    FILE *rom_file = fopen(argv[1], "r");
    if (!rom_file) {
        printf("Error opening file %s\n", argv[1]);
        return 1;
    }
    
    uint8_t header[0x10];
    if (fread(header, sizeof(header), 1, rom_file) < 1) {
        printf("Error reading iNES header\n");
        return 1;
    }
    if (strncmp((const char *)header, "NES\x1a", 4)) {
        printf("Not a iNES file\n");
        return 1;
    }
    
    Cartridge cart;
    
    int size = header[4] * 16;
    printf("PRG ROM: %dKB\n", size);
    if (size <= 0) {
        printf("Unexpected zero size for PRG ROM\n");
        return 1;
    }
    cart.prg_rom_size = size * 1024;
    cart.prg_rom = malloc(cart.prg_rom_size);
    if (fread(cart.prg_rom, cart.prg_rom_size, 1, rom_file) < 1) {
        printf("Error reading PRG ROM\n");
        return 1;
    }
    
    size = header[5] * 8;
    printf("CHR ROM: %dKB\n", size);
    cart.chr_rom_size = size * 1024;
    if (cart.chr_rom_size > 0) {
        cart.chr_rom = malloc(cart.chr_rom_size);
        if (fread(cart.chr_rom, cart.chr_rom_size, 1, rom_file) < 1) {
            printf("Error reading CHR ROM\n");
            return 1;
        }
    } else {
        cart.chr_rom = NULL;
    }
    
    cart.mapper = ((header[6] & 0b11110000) >> 4) + (header[7] & 0b11110000);
    printf("Mapper: %d\n", cart.mapper);
    if (cart.mapper) {
        printf("Only NROM (aka. iNES mapper 0) is supported so far\n");
        return 1;
    }
    
    cart.mirroring = header[6] & 1;
    printf("Mirroring: %s\n", (cart.mirroring ? "Vertical" : "Horizontal"));
    fclose(rom_file);
    
    DebugMap *dbg_map = NULL;
    if (argc >= 3) {
        dbg_map = malloc(sizeof(DebugMap) * 2000); // TODO: figure out size
        FILE *map_file = fopen(argv[2], "r");
        int i = 0;
        while (fscanf(map_file, "%255s @ %4hx", dbg_map[i].label,
                      &(dbg_map[i].addr)) == 2) {
            i++;
        }
        dbg_map[i + 1].label[0] = 0;
        printf("Read %d entries from %s\n", i, argv[2]);
        fclose(map_file);
    }
    
    machine_loop(&cart, dbg_map, verbose);
        
    if (dbg_map) {
        free(dbg_map);
    }
    if (cart.chr_rom) {
        free(cart.chr_rom);
    }
    free(cart.prg_rom);
    
    return 0;
}
