CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -I./include
LDFLAGS=-g -L./build/src
LDLIBS=
RM=rm
BUILD_DIR=./build

DOCS_HTML_INDEX_PATH=docs/html/index.html

.PHONY: all

all: lib test docs

docs: $(DOCS_HTML_INDEX_PATH)

$(DOCS_HTML_INDEX_PATH):
	$(MAKE) -C	docs 	docs

lib:
	$(MAKE) -C src

test:
	$(MAKE) -C $@
	$(BUILD_DIR)/test/test_gc

coverage: test
	$(MAKE) -C	test 	coverage

coverage-html: coverage
	$(MAKE) -C	test 	coverage-html

clean:
	$(MAKE) -C	docs 	clean
	$(MAKE) -C	src		clean
	$(MAKE) -C	test 	clean

distclean: clean
	$(MAKE) -C	test	distclean

install:
	$(MAKE) -C	src		install

uninstall:
	$(MAKE) -C	src		uninstall
