#include "loader.h"

#include "../driver.h"
#include "cartridge.h"
#include "machine.h"

int ines_loader(Driver *driver, uint8_t *rom_data, int rom_data_size) {
    Cartridge cart;
    memset(&cart, 0, sizeof(Cartridge));

    int size = rom_data[4] * 16;
    fprintf(stderr, "PRG ROM: %dKB\n", size);
    if (size <= 0) {
        fprintf(stderr, "Unexpected zero size for PRG ROM\n");
        return 1;
    }
    cart.prg_rom_size = size << 10;
    
    size = rom_data[5] * 8;
    fprintf(stderr, "CHR ROM: ");
    if (size) {
        fprintf(stderr, "%dKB\n", size);
    } else {
        fprintf(stderr, "None (uses RAM instead)\n");
    }
    cart.chr_memory_size = size << 10;

    int expected_size = cart.prg_rom_size + cart.chr_memory_size + HEADER_SIZE;
    if (expected_size > rom_data_size) {
        fprintf(stderr,
                "Expected total file size (%d) exceeds actual file size (%d)\n",
                expected_size, rom_data_size);
        return 1;
    }
    
    cart.prg_rom = rom_data + HEADER_SIZE;
    if (cart.chr_memory_size) {
        cart.chr_memory = cart.prg_rom + cart.prg_rom_size;
    }
    
    cart.mapper_id = (rom_data[6] >> 4) | (rom_data[7] & 0b11110000);
    const char *mapper_name = "Unidentified";
    bool supported = mapper_check_support(cart.mapper_id, &mapper_name);
    fprintf(stderr, "Mapper: %d (%s)\n", cart.mapper_id, mapper_name);
    if (!supported) {
        fprintf(stderr, "Unsupported mapper ID\n");
        return 1;
    }

    const char *nm_desc;
    if (rom_data[6] & 0b1000) {
        cart.default_mirroring = NT_FOUR;
        nm_desc = "Four-screen";
    } else {
        int mirroring_flag = rom_data[6] & 1;
        cart.default_mirroring = (mirroring_flag ? NT_VERTICAL : NT_HORIZONTAL);
        nm_desc = (mirroring_flag ? "Vertical" : "Horizontal");
    }
    fprintf(stderr, "Mirroring: %s\n", nm_desc);

    cart.has_battery_backup = rom_data[6] & 0b10;
    fprintf(stderr, "Battery-backed SRAM: %s\n",
          (cart.has_battery_backup ? "Yes" : "No"));

    driver->screen = malloc(sizeof(uint32_t) * WIDTH * HEIGHT);
    driver->screen_w = WIDTH;
    driver->screen_h = HEIGHT;
    Machine *vm = malloc(sizeof(Machine));
    machine_init(vm, &cart, &driver->input, driver->screen);
    driver->vm = vm;
    driver->advance_frame_func = (AdvanceFrameFuncPtr)machine_advance_frame;
    driver->teardown_func = f_teardown;
    return 0;
}

void f_teardown(Driver *driver) {
    Machine *vm = driver->vm;
    machine_teardown(vm);
    free(driver->vm);
    free(driver->screen);
}
