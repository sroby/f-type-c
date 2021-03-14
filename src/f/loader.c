#include "loader.h"

#include "../driver.h"
#include "cartridge.h"
#include "machine.h"

int ines_loader(Driver *driver, blob *rom) {
    FCartInfo cart;
    memset(&cart, 0, sizeof(FCartInfo));

    int size = rom->data[4] * 16;
    eprintf("PRG ROM: %dKB\n", size);
    if (size <= 0) {
        eprintf("Unexpected zero size for PRG ROM\n");
        return 1;
    }
    cart.prg_rom.size = size << 10;
    
    size = rom->data[5] * 8;
    eprintf("CHR ROM: ");
    if (size) {
        eprintf("%dKB\n", size);
    } else {
        eprintf("None (uses RAM instead)\n");
    }
    cart.chr_rom.size = size << 10;

    size_t expected_size = cart.prg_rom.size + cart.chr_rom.size + HEADER_SIZE;
    if (expected_size > rom->size) {
        eprintf("Expected total file size (%zu) "
                "exceeds actual file size (%zu)\n",
                expected_size, rom->size);
        return 1;
    }
    
    cart.prg_rom.data = rom->data + HEADER_SIZE;
    if (cart.chr_rom.size) {
        cart.chr_rom.data = cart.prg_rom.data + cart.prg_rom.size;
    }
    
    if (BIT_CHECK(rom->data[7], 3) && !BIT_CHECK(rom->data[7], 2)) {
        eprintf("FILE HAS NES 2.0 HEADER!!\n");
    }
    
    cart.mapper_id = (rom->data[6] >> 4) | (rom->data[7] & 0b11110000);
    const char *mapper_name = "Unidentified";
    bool supported = mapper_check_support(cart.mapper_id, &mapper_name);
    eprintf("Mapper: %d (%s)\n", cart.mapper_id, mapper_name);
    if (!supported) {
        eprintf("Unsupported mapper ID\n");
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
    eprintf("Mirroring: %s\n", nm_desc);

    cart.has_battery_backup = rom->data[6] & 0b10;
    eprintf("Battery-backed SRAM: %s\n",
          (cart.has_battery_backup ? "Yes" : "No"));

    driver->screen_w = WIDTH;
    driver->screen_h = HEIGHT_CROPPED;
    Machine *vm = malloc(sizeof(Machine));
    machine_init(vm, &cart, driver);
    driver->vm = vm;
    driver->refresh_rate = REFRESH_RATE;
    driver->screens[0] = vm->ppu.screens[0];
    driver->screens[1] = vm->ppu.screens[1];
    driver->advance_frame_func = (AdvanceFrameFuncPtr)machine_advance_frame;
    driver->teardown_func = f_teardown;
    return 0;
}

void f_teardown(Driver *driver) {
    Machine *vm = driver->vm;
    machine_teardown(vm);
    free(driver->vm);
}
