#include "machine.h"

#include "cpu.h"
#include "cartridge.h"
#include "memory_maps.h"
#include "ppu.h"

void machine_init(Machine *vm, CPUState *cpu, PPUState *ppu, MemoryMap *cpu_mm,
                  MemoryMap *ppu_mm, Cartridge *cart, const DebugMap *dbg_map) {
    memset(vm, 0, sizeof(Machine));
    vm->controller_bit[0] = vm->controller_bit[1] = 8;
    
    vm->cpu = cpu;
    vm->ppu = ppu;
    vm->cpu_mm = cpu_mm;
    vm->ppu_mm = ppu_mm;
    vm->cart = cart;
    vm->dbg_map = dbg_map;

    machine_set_nt_mirroring(vm, cart->default_mirroring);
    mapper_init(vm);
    
    cpu_reset(vm->cpu);
}

bool machine_advance_frame(Machine *vm, bool verbose) {
    bool done;
    do {
        if (vm->ppu->time > vm->cpu->time * T_CPU_MULTIPLIER) {
            // Check for debug label
            bool is_endless_loop = false;
            if (verbose && vm->dbg_map) {
                int i = 0;
                while (vm->dbg_map[i].label[0]) {
                    if (vm->dbg_map[i].addr == vm->cpu->pc) {
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
            if (cpu_step(vm->cpu, verbose && !is_endless_loop) != 0x100) {
                return false;
            }

        }
        
        done = ppu_step(vm->ppu, verbose);
    } while (!done);
    return true;
}

void machine_set_nt_mirroring(Machine *vm, NametableMirroring nm) {
    static const int layouts[] = {
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
