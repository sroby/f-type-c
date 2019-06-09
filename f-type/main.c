#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cpu.h"

int main(int argc, const char * argv[]) {
    const bool verbose = *getenv("VERBOSE") - '0';
    
    if (argc != 2) {
        printf("Usage: %s rom.nes\n", argv[0]);
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
    
    int prg_size = header[4] * 16;
    printf("PRG ROM: %dKB\n", prg_size);
    if (prg_size <= 0) {
        printf("Unexpected size for PRG ROM\n");
        return 1;
    }
    prg_size *= 1024;
    uint8_t *prg_rom = malloc(prg_size < SIZE_PRG_ROM ? SIZE_PRG_ROM : prg_size);
    if (fread(prg_rom, prg_size, 1, rom_file) < 1) {
        printf("Error reading PRG ROM\n");
        return 1;
    }
    if (prg_size < SIZE_PRG_ROM) {
        // Duplicate the rom if it's just 16kB
        memcpy(prg_rom + prg_size, prg_rom, prg_size);
    }
    
    int chr_size = header[5] * 8;
    printf("CHR ROM: %dKB\n", chr_size);
    chr_size *= 1024;
    uint8_t *chr_rom = NULL;
    if (chr_size >= 0) {
        chr_rom = malloc(chr_size);
        if (fread(chr_rom, chr_size, 1, rom_file) < 1) {
            printf("Error reading CHR ROM\n");
            return 1;
        }
    }
    int mapper = ((header[6] & 0b11110000) >> 4) + (header[7] & 0b11110000);
    printf("Mapper: %d\n", mapper);
    if (mapper) {
        printf("Only mapper 0 is supported so far\n");
        return 1;
    }
    bool mirroring = header[6] & 1;
    printf("Mirroring: %s\n", (mirroring ? "Vertical" : "Horizontal"));
    fclose(rom_file);
    
    memory_map mm;
    memory_map_cpu_init(&mm, prg_rom);
    
    cpu_state st;
    cpu_init(&st, &mm);
    
    int total_t = cpu_reset(&st);
    do {
        if (verbose) {
            cpu_debug_print_state(&st);
        }
        total_t += cpu_step(&st, verbose);
    } while(st.pc != 0x8057);
    
    if (verbose) {
        cpu_debug_print_state(&st);
    }
    printf("Ended in %d cycles\n", total_t);
    
    if (chr_rom) {
        free(chr_rom);
    }
    free(prg_rom);
    return 0;
}
