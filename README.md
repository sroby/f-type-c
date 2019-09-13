# f-type

**f-type** is a work-in-progress emulator for Nintendo's 8-bit 6502 family of systems (NES, Family Computer, Vs. System, Playchoice-10). It is written entirely from scratch, using existing documentation available online.

## Current features
* As platform-agnostic as possible, should compile and run on any platform supported by SDL2
* Mostly complete, mostly accurate NTSC/RGB PPU rendering
* Regular controller input
* Lightgun input (via mouse)
* Mapper support:
    * First-party: 0, 1, 2, 3, 4, 7, 9, 10, 13, 34, 66, 94, 99, 155, 180, 185 (all except the MMC5 and some multicarts)
    * Third-party: 11, 38, 39, 68, 70, 75, 79, 87, 89, 93, 97, 113, 140, 146, 151, 152, 184

## Feature wishlist
* Sound!
* Debugger!
* ...and lots more

## Dependencies
* SDL 2.0.x
* ...that's all for now

## Compiling

### Linux

Make sure you have the SDL2 devel package installed via your distribution's package manager.

A simple Makefile is included, just run:

    $ make

### macOS

Install the dependencies via [Homebrew](https://brew.sh):

    $ brew install sdl2

An Xcode project is included, but the same Makefile as Linux can also be used.

### Windows
*TODO*

## Documentation credits
This project wouldn't be possible without the following sources:
* [Nesdev Wiki](http://wiki.nesdev.com/w/index.php/Nesdev_Wiki)
* [Everynes NES Hardware Specifications](http://problemkaputt.de/everynes.htm)
