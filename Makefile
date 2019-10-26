CC=gcc
SDL2_CONFIG=sdl2-config
CFLAGS=-O3 -Wall -Werror `$(SDL2_CONFIG) --cflags`
LDFLAGS=`$(SDL2_CONFIG) --libs`
BUILD_ID=`git rev-parse --short HEAD`

TARGET=f-type
SRCS= \
	src/cartridge.c \
	src/cpu.c \
	src/machine.c \
	src/main.c \
	src/memory_maps.c \
	src/ppu.c \
	src/window.c

all: $(TARGET)

debug: CFLAGS += -DDEBUG -g
debug: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS) -DBUILD_ID=\"$(BUILD_ID)\"

clean:
	$(RM) $(TARGET)
