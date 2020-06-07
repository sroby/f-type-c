#include "loader.h"

#include "../driver.h"
#include "cartridge.h"
#include "machine.h"

int ines_loader(Driver *driver, blob *rom) {
    FCartInfo cart;
    memset(&cart, 0, sizeof(FCartInfo));

    int size = rom->data[4] * 16;
    fprintf(stderr, "PRG ROM: %dKB\n", size);
    if (size <= 0) {
        fprintf(stderr, "Unexpected zero size for PRG ROM\n");
        return 1;
    }
    cart.prg_rom.size = size << 10;
    
    size = rom->data[5] * 8;
    fprintf(stderr, "CHR ROM: ");
    if (size) {
        fprintf(stderr, "%dKB\n", size);
    } else {
        fprintf(stderr, "None (uses RAM instead)\n");
    }
    cart.chr_rom.size = size << 10;

    size_t expected_size = cart.prg_rom.size + cart.chr_rom.size + HEADER_SIZE;
    if (expected_size > rom->size) {
        fprintf(stderr,
            "Expected total file size (%zu) exceeds actual file size (%zu)\n",
                expected_size, rom->size);
        return 1;
    }
    
    cart.prg_rom.data = rom->data + HEADER_SIZE;
    if (cart.chr_rom.size) {
        cart.chr_rom.data = cart.prg_rom.data + cart.prg_rom.size;
    }
    
    cart.mapper_id = (rom->data[6] >> 4) | (rom->data[7] & 0b11110000);
    const char *mapper_name = "Unidentified";
    bool supported = mapper_check_support(cart.mapper_id, &mapper_name);
    fprintf(stderr, "Mapper: %d (%s)\n", cart.mapper_id, mapper_name);
    if (!supported) {
        fprintf(stderr, "Unsupported mapper ID\n");
        return 1;
    }

    const char *nm_desc;
    if (rom->data[6] & 0b1000) {
        cart.default_mirroring = NT_FOUR;
        nm_desc = "Four-screen";
    } else {
        int mirroring_flag = rom->data[6] & 1;
        cart.default_mirroring = (mirroring_flag ? NT_VERTICAL : NT_HORIZONTAL);
        nm_desc = (mirroring_flag ? "Vertical" : "Horizontal");
    }
    fprintf(stderr, "Mirroring: %s\n", nm_desc);

    cart.has_battery_backup = rom->data[6] & 0b10;
    fprintf(stderr, "Battery-backed SRAM: %s\n",
          (cart.has_battery_backup ? "Yes" : "No"));

    driver->screen = malloc(sizeof(uint32_t) * WIDTH * HEIGHT);
    driver->screen_w = WIDTH;
    driver->screen_h = HEIGHT;
    Machine *vm = malloc(sizeof(Machine));
    machine_init(vm, &cart, driver);
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
