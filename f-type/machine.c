#include <string.h>

#include "machine.h"
#include "cpu.h"
#include "ppu.h"
#include "memory_maps.h"

void machine_loop(const Cartridge *cart, const DebugMap *dbg_map,
                  bool verbose) {
    CPUState cpu;
    PPUState ppu;
    MemoryMap cpu_mm, ppu_mm;
    MemoryMapCPUInternal cpu_mm_i;
    MemoryMapPPUInternal ppu_mm_i;

    cpu_init(&cpu, &cpu_mm);
    ppu_init(&ppu, &ppu_mm, &cpu);
    memory_map_cpu_init(&cpu_mm, &cpu_mm_i, cart, &ppu);
    memory_map_ppu_init(&ppu_mm, &ppu_mm_i, cart);
    
    cpu_reset(&cpu);
    bool is_endless_loop;
    while (true) {
        /*if (verbose) {
            cpu_debug_print_state(&cpu);
        }*/
        is_endless_loop = false;
        if (verbose && dbg_map) {
            int i = 0;
            while (dbg_map[i].label[0]) {
                if (dbg_map[i].addr == cpu.pc) {
                    const char *label = dbg_map[i].label;
                    if (strcmp(label, "EndlessLoop")) {
                        printf(":%s\n", dbg_map[i].label);
                    } else {
                        is_endless_loop = true;
                    }
                    break;
                }
                i++;
            }
        }
        if (cpu_step(&cpu, verbose && !is_endless_loop) < 0x100) {
            break;
        }
        while (ppu.t * T_SCANLINE_PER_CPU < cpu.t * T_MULTI) {
            if (verbose) {
                printf("--scanline %d--\n", ppu.scanline);
            }
            ppu_scanline(&ppu);
        }
    }
    
    /*if (verbose) {
        cpu_debug_print_state(&cpu);
    }*/
    printf("Ended with %llu CPU cycles\n", cpu.t);
}
