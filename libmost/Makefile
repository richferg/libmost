CC      ?= gcc
AR      ?= ar
CFLAGS  ?= -Wall -Wextra -O2 -fPIC -Iinclude
LDFLAGS ?=

LIBNAME := libmost

BUILD_ROOT ?= ../build
DIST_ROOT  ?= ../dist
BUILD_DIR  := $(BUILD_ROOT)/libmost
DIST_DIR   := $(DIST_ROOT)/libmost

SRC := src/most.c
OBJ := $(patsubst src/%.c,$(BUILD_DIR)/src/%.o,$(SRC))

LIBA_BUILD := $(BUILD_DIR)/$(LIBNAME).a
LIBS_BUILD := $(BUILD_DIR)/$(LIBNAME).so
LIBA_DIST  := $(DIST_DIR)/$(LIBNAME).a
LIBS_DIST  := $(DIST_DIR)/$(LIBNAME).so

.DEFAULT_GOAL := all
.PHONY: all clean

all: $(LIBA_DIST) $(LIBS_DIST)

$(BUILD_DIR)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBA_BUILD): $(OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJ)

$(LIBS_BUILD): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -shared -o $@ $(OBJ)

$(LIBA_DIST): $(LIBA_BUILD)
	@mkdir -p $(dir $@)
	cp -f $< $@

$(LIBS_DIST): $(LIBS_BUILD)
	@mkdir -p $(dir $@)
	cp -f $< $@

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
