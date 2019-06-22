CC=gcc
CFLAGS=-O3 -Wall -Werror `sdl2-config --cflags --libs`

TARGET=ftype
SRCS= \
	f-type/cpu.c \
	f-type/machine.c \
	f-type/main.c \
	f-type/memory_maps.c \
	f-type/ppu.c \
	f-type/window.c

all: $(TARGET)

debug: CFLAGS += -DDEBUG -g
debug: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	$(RM) $(TARGET)
