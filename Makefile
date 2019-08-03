CC=gcc
CFLAGS=-O3 -Wall -Werror `sdl2-config --cflags`
LDFLAGS=`sdl2-config --libs`

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
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	$(RM) $(TARGET)
