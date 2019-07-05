#include <string.h>

#include "machine.h"

#include "cartridge.h"

void machine_init(Machine *vm, Cartridge *cart, const DebugMap *dbg_map) {
    cpu_init(&vm->cpu, &vm->cpu_mm);
    ppu_init(&vm->ppu, &vm->ppu_mm, &vm->cpu);
    memory_map_cpu_init(&vm->cpu_mm, cart, &vm->ppu);
    memory_map_ppu_init(&vm->ppu_mm, cart);
    mapper_init(cart, &vm->cpu_mm, &vm->ppu_mm);
    
    cpu_reset(&vm->cpu);
}

void machine_advance_frame(Machine *vm, bool verbose) {
    bool done = false;
    do {
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
        cpu_step(&vm->cpu, verbose && !is_endless_loop);
        // Check for PPU scanline, and possibly end of frame
        while (vm->ppu.t * T_SCANLINE_PER_CPU < vm->cpu.t * T_MULTI) {
            if (verbose) {
                printf("--scanline %d--\n", vm->ppu.scanline);
            }
            if (ppu_scanline(&vm->ppu)) {
                done = true;
            }
        }
    } while (!done);
}
