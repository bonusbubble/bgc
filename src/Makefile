CC=clang
CFLAGS=-g -Wall -Wextra -pedantic -I../include -fPIC
LDFLAGS=-g -L../build/src -L../build/test -fPIC
LDLIBS=
CP=cp
MKDIR=mkdir
RM=rm
BUILD_DIR=../build
DIST_DIR=../dist

ROOT=/usr/local

PROJECT_NAME=bgc

LIB_PREFIX=lib
LIB_NAME=$(LIB_PREFIX)$(PROJECT_NAME)
STATIC_LIB_EXT=.a
STATIC_LIBRARY=$(LIB_NAME)$(STATIC_LIB_EXT)
DYNAMIC_LIB_EXT=.so
DYNAMIC_LIBRARY=$(LIB_NAME)$(DYNAMIC_LIB_EXT)

STATIC_LIBRARY_PATH=$(DIST_DIR)/lib/$(STATIC_LIBRARY)
DYNAMIC_LIBRARY_PATH=$(DIST_DIR)/lib/$(DYNAMIC_LIBRARY)

INSTALL_STATIC_LIBRARY_PATH=$(ROOT)/lib/$(STATIC_LIBRARY)
INSTALL_DYNAMIC_LIBRARY_PATH=$(ROOT)/lib/$(DYNAMIC_LIBRARY)

INSTALL_INCLUDE_DIR=$(ROOT)/include

.PHONY: all
all: clean $(STATIC_LIBRARY_PATH) $(DYNAMIC_LIBRARY_PATH)

$(BUILD_DIR)/obj/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -c $< -o $@

SRCS=bgc.c
OBJS=$(SRCS:%.c=$(BUILD_DIR)/obj/%.o)
DEPS=$(OBJS:%.o=%.d)

$(STATIC_LIBRARY_PATH): $(OBJS)
	mkdir -p $(@D)
	ar rcs $@ $^

$(DYNAMIC_LIBRARY_PATH): $(OBJS)
	mkdir -p $(@D)
	$(CC) $(LDFLAGS) $(LDLIBS) -shared -fPIC $^ -o $@

clean:
	$(RM) -f $(OBJS) $(DEPS)

cleanall: clean
	rm -rf $(DIST_DIR)
	mkdir -p $(DIST_DIR)

install: uninstall
	$(CP) -f $(STATIC_LIBRARY_PATH) $(INSTALL_STATIC_LIBRARY_PATH)
	$(CP) -f $(DYNAMIC_LIBRARY_PATH) $(INSTALL_DYNAMIC_LIBRARY_PATH)
	$(MKDIR) -p $(INSTALL_INCLUDE_DIR)
	$(CP) -f ../include/*.h $(INSTALL_INCLUDE_DIR)

uninstall:
	$(RM) -f $(INSTALL_STATIC_LIBRARY_PATH)
	$(RM) -f $(INSTALL_DYNAMIC_LIBRARY_PATH)
	$(RM) -f $(INSTALL_INCLUDE_DIR)/bgc*.h
	$(RM) -rf $(INSTALL_INCLUDE_DIR)/bgc/
