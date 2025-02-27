CC=clang
GENHTML=genhtml
LCOV=lcov
MKDIR=mkdir
OPEN=open
RM=rm

BUILD_DIR=../build
INCLUDE_DIR=../include

CFLAGS=-g -Wall -Wextra -pedantic -I$(INCLUDE_DIR) -fprofile-arcs -ftest-coverage
LDFLAGS=-g -L../build/lib --coverage
LDLIBS=-lbgc


.PHONY: all
all: $(BUILD_DIR)/test/test_gc $(BUILD_DIR)/test/stress_test_gc

$(BUILD_DIR)/test/%.o: %.c
	$(MKDIR) -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

SRCS=test_gc.c
OBJS=$(SRCS:%.c=$(BUILD_DIR)/test/%.o)
DEPS=$(OBJS:%.o=%.d)

$(BUILD_DIR)/test/test_gc: $(OBJS)
	$(MKDIR) -p $(@D)
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@

$(BUILD_DIR)/test/stress_test_gc: stress_test_gc.c
	$(MKDIR) -p $(@D)
	$(CC) $(LDFLAGS) $(LDLIBS) $^ -o $@ -lbgc

coverage: $(BUILD_DIR)/test/test_gc
	$(LCOV) -b . -d ../build/test/ -c -o ../build/test/coverage-all.info
	$(LCOV) -b . -r ../build/test/coverage-all.info "*test*" -o ../build/test/coverage.info

coverage-html: coverage
	$(MKDIR) -p ../build/test/coverage
	$(GENHTML) -o ../build/test/coverage ../build/test/coverage.info
	$(OPEN) ../build/test/coverage/index.html

.PHONY: clean
clean:
	$(RM) -f $(OBJS) $(DEPS)
	$(RM) -f *.gcda
	$(RM) -f *.gcno

distclean: clean
	$(RM) -f $(BUILD_DIR)/test/test_gc
	$(RM) -f $(BUILD_DIR)/test/*gcda
	$(RM) -f $(BUILD_DIR)/test/*gcno
