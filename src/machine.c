#include <string.h>

#include "machine.h"

#include "cartridge.h"
#include "cpu.h"
#include "memory_maps.h"
#include "ppu.h"
#include "window.h"

void machine_loop(Cartridge *cart, const DebugMap *dbg_map, Window *wnd) {
    // Create and initialize the hardware
    CPUState cpu;
    PPUState ppu;
    MemoryMap cpu_mm, ppu_mm;
    
    cpu_init(&cpu, &cpu_mm);
    ppu_init(&ppu, &ppu_mm, &cpu);
    memory_map_cpu_init(&cpu_mm, cart, &ppu);
    memory_map_ppu_init(&ppu_mm, cart);
    cart_init(cart, &cpu_mm, &ppu_mm);

    const char *const verb_char = getenv("VERBOSE");
    const bool verbose = verb_char ? *verb_char - '0' : false;

    cpu_reset(&cpu);

    const uint64_t ticks_per_frame = SDL_GetPerformanceFrequency() *  10000
                                                                   / 600988;
    
    // Main loop
    int frame = 0;
    uint64_t next_frame = SDL_GetPerformanceCounter();
    while(true) {
        // Process events
        if (window_process_events(wnd, cpu_mm.data.cpu.controllers)) {
            break;
        }
        
        if (SDL_GetPerformanceCounter() < next_frame) {
            SDL_Delay(1);
            continue;
        }
        next_frame += ticks_per_frame;
        
        // Advance one frame
        bool done = false;
        do {
            // Check for debug label
            bool is_endless_loop = false;
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
            // Run next CPU instruction
            cpu_step(&cpu, verbose && !is_endless_loop);
            // Check for PPU scanline, and possibly end of frame
            while (ppu.t * T_SCANLINE_PER_CPU < cpu.t * T_MULTI) {
                if (verbose) {
                    printf("--scanline %d--\n", ppu.scanline);
                }
                if (ppu_scanline(&ppu)) {
                    done = true;
                }
            }
        } while (!done);
        
        // Render the frame
        window_update_screen(wnd, &ppu);
        frame++;
    }
    
    printf("Ended after %d frames\n", frame);
}
