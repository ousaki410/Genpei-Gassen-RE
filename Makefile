CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -Iinclude -Ithird_party/include
LDFLAGS ?= -Lthird_party/lib -Wl,-rpath,'$$ORIGIN/../third_party/lib' -lSDL2

SRCS = src/hal_video.c src/decompress.c src/main.c
OBJS = $(SRCS:.c=.o)
TARGET = bin/yphz_engine

all: $(TARGET)

bin:
	mkdir -p bin

$(TARGET): bin $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	@echo "[+] Built $@"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET) bin

.PHONY: all clean bin
