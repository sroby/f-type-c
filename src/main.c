#include "common.h"
#include <libgen.h>

#include "f/loader.h"
#include "s/loader.h"
#include "driver.h"
#include "window.h"

int main(int argc, char *argv[]) {
    eprintf("%s build %s (%s)\n", APP_NAME, BUILD_ID, APP_HOMEPAGE);
    if (argc < 2) {
        eprintf("Usage: %s rom_file [debug.map]\n", argv[0]);
        return 1;
    }
    
    // Load the entire file in memory
    FILE *rom_file = fopen(argv[1], "rb");
    if (!rom_file) {
        eprintf("%s: Error opening file\n", argv[1]);
        return 1;
    }
    if (fseeko(rom_file, 0, SEEK_END)) {
        eprintf("%s: Error determining file size\n", argv[1]);
        return 1;
    }
    blob rom;
    rom.size = ftello(rom_file);
    if (rom.size < 1024) {
        eprintf("%s: File is too small\n", argv[1]);
        return 1;
    }
    if (fseeko(rom_file, 0, SEEK_SET)) {
        eprintf("%s: Error seeking file\n", argv[1]);
        return 1;
    }
    rom.data = malloc(rom.size);
    if (fread(rom.data, rom.size, 1, rom_file) < 1) {
        eprintf("%s: Error reading file\n", argv[1]);
        return 1;
    }
    fclose(rom_file);

    Driver driver;
    memset(&driver, 0, sizeof(Driver));
    driver.input.lightgun_pos = -1;
    
    // Identify file type and pass to the appropriate loader
    int error_code = 1;
    eprintf("%s: ", argv[1]);
    if (!strncmp((const char *)rom.data, "NES\x1a", 4)) {
        eprintf("iNES file format\n");
        error_code = ines_loader(&driver, &rom);
    } else if (!strncmp((const char *)rom.data, "FDS\x1a", 4)) {
        eprintf("FDS disk image\n");
    } else {
        error_code = s_loader(&driver, &rom);
    }
    if (error_code) {
        return error_code;
    }
    
    /*DebugMap *dbg_map = NULL;
    if (argc >= 3) {
        dbg_map = malloc(sizeof(DebugMap) * 2000); // TODO: figure out size
        FILE *map_file = fopen(argv[2], "r");
        int i = 0;
        while (fscanf(map_file, "%255s @ %4hx", dbg_map[i].label,
                      &(dbg_map[i].addr)) == 2) {
            i++;
        }
        dbg_map[i + 1].label[0] = 0;
        eprintf("Read %d entries from %s\n", i, argv[2]);
        fclose(map_file);
    }*/
        
    Window wnd;
#ifdef _WIN32
    char *fn = strdup(argv[1]);
#else
    char *fn = realpath(argv[1], NULL);
#endif
    error_code = window_init(&wnd, &driver, basename(fn));
    if (error_code) {
        return error_code;
    }
    
    window_loop(&wnd);
    window_cleanup(&wnd);
    
    free(fn);
    /*if (dbg_map) {
        free(dbg_map);
    }*/
    
    if (driver.teardown_func) {
        (*driver.teardown_func)(&driver);
    }
    free(rom.data);
    
    return 0;
}
