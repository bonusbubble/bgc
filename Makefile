CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -I./include
LDFLAGS=-g -L./build/src
LDLIBS=
RM=rm
BUILD_DIR=./build

.PHONY: lib test

lib:
	$(MAKE) -C src

test:
	$(MAKE) -C $@
	$(BUILD_DIR)/test/test_gc

coverage: test
	$(MAKE) -C	test 	coverage

coverage-html: coverage
	$(MAKE) -C	test 	coverage-html

.PHONY: clean
clean:
	$(MAKE) -C	src		clean
	$(MAKE) -C	test 	clean

distclean: clean
	$(MAKE) -C	test	distclean

install:
	$(MAKE) -C	src		install

uninstall:
	$(MAKE) -C	src		uninstall
