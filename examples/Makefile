CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -I../include -fPIC
LDFLAGS=-g -fPIC
LDLIBS=-lbgc
RM=rm

HELLO_WORLD=hello_world.elf

.PHONY: all

all: clean examples

clean:
	$(RM) -f *.elf

examples: $(HELLO_WORLD)

%.elf: %.c
	$(CC) $^ -o $@ $(CFLAGS) $(LDFLAGS) $(LDLIBS)
