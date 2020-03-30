#include "machine.h"

#include "../input.h"

void machine_init(Machine *vm, Cartridge *cart, InputState *input,
                  uint32_t *screen) {
    memset(vm, 0, sizeof(Machine));
    
    vm->cart = *cart;
    vm->input = input;

    memory_map_cpu_init(&vm->cpu_mm, vm);
    memory_map_ppu_init(&vm->ppu_mm, vm);
    cpu_65xx_init(&vm->cpu, &vm->cpu_mm);
    ppu_init(&vm->ppu, &vm->ppu_mm, &vm->cpu, screen, &input->lightgun_pos);
    
    if (!vm->cart.chr_memory.size) {
        vm->cart.chr_memory.size = SIZE_CHR_ROM;
        vm->cart.chr_is_ram = true;
        vm->cart.chr_memory.data = malloc(SIZE_CHR_ROM);
        memset(vm->cart.chr_memory.data, 0, SIZE_CHR_ROM);
    }
    
    machine_set_nt_mirroring(vm, vm->cart.default_mirroring);
    mapper_init(vm);
    
    cpu_65xx_reset(&vm->cpu, false);
}

void machine_teardown(Machine *vm) {
    // TODO: Save SRAM
    if (vm->cart.sram.data) {
        free(vm->cart.sram.data);
    }
    
    if (vm->cart.chr_is_ram) {
        free(vm->cart.chr_memory.data);
    }
}

bool machine_advance_frame(Machine *vm, bool verbose) {
    bool done;
    do {
        if (vm->ppu.time > vm->cpu.time * T_CPU_MULTIPLIER) {
            // Check for debug label
            bool is_endless_loop = false;
            if (verbose && vm->dbg_map) {
                int i = 0;
                while (vm->dbg_map[i].label[0]) {
                    if (vm->dbg_map[i].addr == vm->cpu.pc) {
                        const char *label = vm->dbg_map[i].label;
                        if (strcmp(label, "EndlessLoop")) {
                            printf(":%s\n", vm->dbg_map[i].label);
                        } else {
                            is_endless_loop = true;
                        }
                        break;
                    }
                    i++;
                }
            }
            
            // Run next CPU instruction
            if (cpu_65xx_step(&vm->cpu, verbose && !is_endless_loop) != 0x100) {
                return false;
            }
        }
        
        done = ppu_step(&vm->ppu, verbose);
    } while (!done);
    return true;
}

void machine_set_nt_mirroring(Machine *vm, NametableMirroring nm) {
    const int layouts[] = {
        0, 0, 0, 0, // SINGLE_A
        1, 1, 1, 1, // SINGLE_B
        0, 1, 0, 1, // VERTICAL
        0, 0, 1, 1, // HORIZONTAL
        0, 1, 2, 3, // FOUR
    };
    const int *layout = layouts + nm * 4;
    for (int i = 0; i < 4; i++) {
        vm->nt_layout[i] = vm->nametables[layout[i]];
    }
}
