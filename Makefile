CC=gcc
SDL2_CONFIG=sdl2-config
CFLAGS=-O3 -Wall -Werror `$(SDL2_CONFIG) --cflags`
LDFLAGS=`$(SDL2_CONFIG) --libs`
BUILD_ID=`git rev-parse --short HEAD`

TARGET=f-type
SRCS= \
	src/cpu/65xx.c \
	src/f/cartridge.c \
	src/f/loader.c \
	src/f/machine.c \
	src/f/memory_maps.c \
	src/f/ppu.c \
	src/main.c \
	src/window.c

all: $(TARGET)

debug: CFLAGS += -DDEBUG -g
debug: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS) -DBUILD_ID=\"$(BUILD_ID)\"

clean:
	$(RM) $(TARGET)
