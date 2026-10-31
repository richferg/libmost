CC       ?= gcc
CFLAGS   ?= -Wall -Wextra -O2
CPPFLAGS ?= -I../libmost/include -I$(BUILD_DIR)
LDFLAGS  ?=
LDLIBS   ?=

BUILD_ROOT ?= ../build
DIST_ROOT  ?= ../dist
BUILD_DIR  := $(BUILD_ROOT)/mostd
DIST_DIR   := $(DIST_ROOT)/mostd
DIST_BIN   := $(DIST_DIR)/bin/mostd

LIBMOST_DIR := ../libmost
LIBMOST_A   := $(DIST_ROOT)/libmost/libmost.a

SRC := \
	main.c \
	log.c \
	protocol/addrbook.c \
	protocol/catalog.c \
	protocol/control.c \
	protocol/decode.c \
	protocol/diagnosis.c \
	protocol/netblock_server.c \
	protocol/nodecfg.c \
	protocol/registry.c \
	protocol/rx.c \
	protocol/runtime.c \
	protocol/sdars.c \
	protocol/tx.c \
	protocol/transport.c
OBJ := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRC))
BIN := $(BUILD_DIR)/mostd
BUILD_INFO_H := $(BUILD_DIR)/build_info.h

.DEFAULT_GOAL := all
.PHONY: all clean test FORCE

all: $(DIST_BIN)

test:
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		tests/sdars_state_test.c -Wl,--gc-sections -o $(BUILD_DIR)/tests/sdars_state_test
	$(BUILD_DIR)/tests/sdars_state_test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		tests/control_test.c -Wl,--gc-sections -o $(BUILD_DIR)/tests/control_test
	$(BUILD_DIR)/tests/control_test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		tests/tx_segment_test.c -Wl,--gc-sections -o $(BUILD_DIR)/tests/tx_segment_test
	$(BUILD_DIR)/tests/tx_segment_test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		tests/diagnosis_test.c -Wl,--gc-sections -o $(BUILD_DIR)/tests/diagnosis_test
	$(BUILD_DIR)/tests/diagnosis_test
	$(CC) $(CPPFLAGS) $(CFLAGS) -ffunction-sections -fdata-sections \
		tests/netblock_advertisement_test.c -Wl,--gc-sections \
		-o $(BUILD_DIR)/tests/netblock_advertisement_test
	$(BUILD_DIR)/tests/netblock_advertisement_test

$(LIBMOST_A):
	$(MAKE) -C $(LIBMOST_DIR)

$(BUILD_INFO_H): FORCE
	@mkdir -p $(dir $@)
	@printf '#ifndef MOSTD_BUILD_INFO_H\n#define MOSTD_BUILD_INFO_H\n#define MOSTD_GIT_HASH "%s"\n#endif\n' "$$(git -C .. rev-parse --short=12 HEAD 2>/dev/null || printf '%s' unknown)" > $@.tmp
	@if ! cmp -s $@.tmp $@; then mv -f $@.tmp $@; else rm -f $@.tmp; fi

# Objects track the headers they include (-MMD): struct layouts such as
# struct mostd_sdars are embedded in other translation units, so a stale
# object silently corrupts memory. Depending on the Makefile forces one full
# rebuild of trees that predate the dependency files.
$(BUILD_DIR)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

$(BUILD_DIR)/main.o: main.c $(BUILD_INFO_H)

$(BIN): $(OBJ) $(LIBMOST_A)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LIBMOST_A) $(LDLIBS)

$(DIST_BIN): $(BIN)
	@mkdir -p $(dir $@)
	cp -f $< $@

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)
