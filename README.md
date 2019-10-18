# f-type

**f-type** is a work-in-progress emulator for Nintendo's 8-bit 6502 family of systems (NES, Family Computer, Vs. System, Playchoice-10). It is written entirely from scratch, using existing documentation available online.

## Current features
* As platform-agnostic as possible, should compile and run on any platform supported by SDL2
* Mostly complete, mostly accurate NTSC/RGB PPU rendering
* Regular controller input
* Lightgun input (via mouse)
* Mapper support:
    * First-party: 0, 1, 2, 3, 4, 7, 9, 10, 13, 34, 66, 94, 99, 119, 155, 180, 185 (all except the MMC5 and some variants)
    * Third-party: 11, 38, 39, 68, 70, 75, 79, 87, 89, 93, 97, 113, 140, 146, 151, 152, 184

## Feature wishlist
* Sound!
* Debugger!
* GUI!
* ...and lots more

## Dependencies
* SDL 2.0.x
* ...that's all for now

## Compiling

### General

A simple Makefile is included, so assuming a Unix-style environment with properly installed dependencies, simply run `make` to build.

See the next sub-sections for platform-specific instructions.

### Linux

Make sure you have the SDL2 devel package installed via your distribution's package manager, then use the Makefile to build.

### macOS

Install the dependencies via [Homebrew](https://brew.sh):

    $ brew install sdl2

In addition to the Makefile, an Xcode project is also included, although it still builds the "Unix way" which means no full-fledged application bundle for now.

### Windows

Building for Windows is only supported via [mingw-w64](http://mingw-w64.org/doku.php) for now (and only 32-bit is confirmed to work). Get the mingw development libraries from the [SDL2 download page](http://libsdl.org/download-2.0.php). To build with the Makefile, you'll probably need to override a few variables to specify the compiler and proper invocation of `sdl2-config`:

    $ export SDL_PATH=<where you extracted SDL2-devel-2.0.x-mingw.tar.gz>/i686-w64-mingw32
    $ make TARGET=f-type.exe CC=i686-w64-mingw32-gcc SDL2_CONFIG="$SDL_PATH/bin/sdl2-config --prefix=$SDL_PATH"

You will also need to copy `SDL2.dll` from `$SDL_PATH/bin` to the same directory as the executable.

## Documentation credits
This project wouldn't be possible without the following sources:
* [Nesdev Wiki](http://wiki.nesdev.com/w/index.php/Nesdev_Wiki)
* [Everynes NES Hardware Specifications](http://problemkaputt.de/everynes.htm)
