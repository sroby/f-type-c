CC=gcc
CFLAGS=-O3 -Wall -Werror

TARGET=ftype
SRCS=f-type/main.c f-type/cpu.c f-type/memory_maps.c

all: $(TARGET)

debug: CFLAGS += -DDEBUG -g
debug: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	$(RM) $(TARGET)
