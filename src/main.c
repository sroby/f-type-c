#include "common.h"
#include <libgen.h>

#include "cpu/65xx.h"
#include "f/cartridge.h"
#include "f/machine.h"
#include "f/memory_maps.h"
#include "f/ppu.h"
#include "window.h"

int main(int argc, char *argv[]) {
    printf("%s build %s (%s)\n", APP_NAME, BUILD_ID, APP_HOMEPAGE);
    if (argc < 2) {
        printf("Usage: %s rom.nes [debug.map]\n", argv[0]);
        return 1;
    }
    
    FILE *rom_file = fopen(argv[1], "rb");
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
    
    const char *nm_desc;
    if (header[6] & 0b1000) {
        cart.default_mirroring = NT_FOUR;
        nm_desc = "Four-screen";
    } else {
        bool mirroring_flag = header[6] & 1;
        cart.default_mirroring = (mirroring_flag ? NT_VERTICAL : NT_HORIZONTAL);
        nm_desc = (mirroring_flag ? "Vertical" : "Horizontal");
    }
    printf("Mirroring: %s\n", nm_desc);
    
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
#ifdef _WIN32
    char *fn = strdup(argv[1]);
#else
    char *fn = realpath(argv[1], NULL);
#endif
    int error_code = window_init(&wnd, basename(fn));
    if (error_code) {
        return error_code;
    }
    
    Machine vm;
    MemoryMap cpu_mm;
    memory_map_cpu_init(&cpu_mm, &vm);
    MemoryMap ppu_mm;
    memory_map_ppu_init(&ppu_mm, &vm);
    CPUState cpu;
    cpu_init(&cpu, &cpu_mm);
    PPUState ppu;
    ppu_init(&ppu, &ppu_mm, &cpu);
    machine_init(&vm, &cpu, &ppu, &cpu_mm, &ppu_mm, &cart, dbg_map);
    
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
