#include "common.h"
#include <libgen.h>

#include "cartridge.h"
#include "machine.h"
#include "window.h"

int main(int argc, const char *argv[]) {
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
    cart.sram = NULL;
    cart.sram_size = 0;
    
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
    cart.chr_memory_size = (size ? size * 1024 : SIZE_CHR_ROM);
    cart.chr_memory = malloc(cart.chr_memory_size);
    cart.chr_is_ram = !size;
    if (cart.chr_is_ram) {
        memset(cart.chr_memory, 0, cart.chr_memory_size);
    } else if (fread(cart.chr_memory, cart.chr_memory_size, 1, rom_file) < 1) {
        printf("Error reading CHR ROM\n");
        return 1;
    }
    fclose(rom_file);

    cart.mapper_id = (header[6] >> 4) + (header[7] & 0b11110000);
    const char *mapper_name = "Unidentified";
    bool supported = mapper_check_support(cart.mapper_id, &mapper_name);
    printf("Mapper: %d (%s)\n", cart.mapper_id, mapper_name);
    if (!supported) {
        printf("Unsupported mapper ID\n");
        return 1;
    }
    
    cart.mirroring = header[6] & 1;
    printf("Mirroring: %s\n", (cart.mirroring ? "Vertical" : "Horizontal"));
    
    cart.has_battery_backup = header[6] & 0b10;
    printf("Battery-backed SRAM: %s\n",
           (cart.has_battery_backup ? "Yes" : "No"));
    
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
    
    Window wnd;
    char *fn = realpath(argv[1], NULL);
    int error_code = window_init(&wnd, basename(fn));
    if (error_code) {
        return error_code;
    }
    
    Machine vm;
    machine_init(&vm, &cart, dbg_map);
    
    window_loop(&wnd, &vm);
    
    window_cleanup(&wnd);
    
    // TODO: Save SRAM
    if (cart.sram) {
        free(cart.sram);
    }
    
    free(fn);
    if (dbg_map) {
        free(dbg_map);
    }
    free(cart.chr_memory);
    free(cart.prg_rom);
    
    return 0;
}
