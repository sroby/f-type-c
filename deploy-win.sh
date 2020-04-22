#!/bin/bash

SDL_VERSION=2.0.12
SDL_PATH=$HOME/Developer/SDL2-$SDL_VERSION/i686-w64-mingw32

cp -f $SDL_PATH/bin/SDL2.dll .
git pull
make TARGET=f-type.exe CC=i686-w64-mingw32-gcc SDL2_CONFIG="$SDL_PATH/bin/sdl2-config --prefix=$SDL_PATH"
cd ..
zip f-type-win32.zip f-type-win32/f-type.exe f-type-win32/SDL2.dll
mv f-type-win32.zip ~/OneDrive/
