CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -I./include
LDFLAGS=-g -L./build/src
LDLIBS=
RM=rm
SUDO=sudo

BUILD_DIR=./build

INDEX_HTML=docs/html/index.html


.PHONY: all

all: clean lib test

release: all docs

debug: all install examples
	./examples/hello_world.elf

docs: $(INDEX_HTML)

$(INDEX_HTML):
	$(SUDO) $(MAKE)	-C	docs	all

lib:
	$(MAKE) -C src

test: test/test_gc

test/test_gc:
	$(MAKE) -C	test	all
	$(BUILD_DIR)/test/test_gc

examples: examples/hello_world.elf

examples/hello_world.elf:
	$(MAKE) -C	examples	all

coverage: test
	$(MAKE) -C	test 	coverage

coverage-html: coverage
	$(MAKE) -C	test 	coverage-html

.PHONY: clean
clean:
	$(MAKE) -C	examples	clean
	$(MAKE) -C	src		clean
	$(MAKE) -C	test 	clean

distclean: clean
	$(MAKE) -C	test	distclean

install:
	$(SUDO) $(MAKE)	-C	src		install

uninstall:
	$(SUDO) $(MAKE) -C	src		uninstall
