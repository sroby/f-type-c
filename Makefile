# Possible usages:
# $ make [DEBUG=1]
# $ make clean

TARGET := f-type
SDLCONFIG := sdl2-config

CFLAGS := -Wall -Werror $(shell $(SDLCONFIG) --cflags --libs) -DBUILD_ID=\"$(shell git rev-parse --short HEAD)\"
ifdef DEBUG
	CFLAGS += -DDEBUG -g
else
	CFLAGS += -O3
endif

SRCS := \
	src/cpu/65xx.c \
	src/f/apu.c \
	src/f/cartridge.c \
	src/f/loader.c \
	src/f/machine.c \
	src/f/memory_maps.c \
	src/f/ppu.c \
	src/s/loader.c \
	src/crc32.c \
	src/main.c \
	src/window.c

INCLUDES := \
	src/common.h \
	src/cpu/65xx.h \
	src/crc32.h \
	src/driver.h \
	src/f/apu.h \
	src/f/cartridge.h \
	src/f/loader.h \
	src/f/machine.h \
	src/f/memory_maps.h \
	src/f/ppu.h \
	src/input.h \
	src/s/loader.h \
	src/window.h

all: $(TARGET)

$(TARGET): $(SRCS) $(INCLUDES)
	$(CC) -o $@ $(SRCS) $(CFLAGS)

clean:
	$(RM) $(TARGET)
