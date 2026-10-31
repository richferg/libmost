CC       ?= gcc
AR       ?= ar
CFLAGS   ?= -Wall -Wextra -O2
LDFLAGS  ?=
LDLIBS   ?=

BUILD_ROOT ?= ../build
DIST_ROOT  ?= ../dist
BUILD_DIR  := $(BUILD_ROOT)/tools
DIST_DIR   := $(DIST_ROOT)/tools

LIBMOST_DIR      ?= ../libmost
MOSTD_DIR        ?= ../mostd
EXAMPLES_SRC_DIR := examples

CPPFLAGS ?= -I$(LIBMOST_DIR)/include -I$(EXAMPLES_SRC_DIR)

LIBMOST_STATIC := $(DIST_ROOT)/libmost/libmost.a
OBJ_EXAMPLES   := $(BUILD_DIR)/obj/examples
OBJ_MOSTD      := $(BUILD_DIR)/obj/mostd
BIN_DIR        := $(BUILD_DIR)/bin
DIST_BIN_DIR   := $(DIST_DIR)/bin

PROGRAMS := most-send sniffer example segtest parse segparse os8104_ctl os8104_aint_probe resource_alloc audio_tx amp_probe deallocate audio_inject whose_channel source_probe

MOST_SEND_OBJS         := $(OBJ_EXAMPLES)/most-send.o
SNIFFER_OBJS           := $(OBJ_EXAMPLES)/most-sniff.o $(OBJ_EXAMPLES)/segmented.o
EXAMPLE_OBJS           := $(OBJ_EXAMPLES)/example.o
SEGTEST_OBJS           := $(OBJ_EXAMPLES)/most-segtest.o $(OBJ_EXAMPLES)/segmented.o
PARSE_OBJS             := $(OBJ_EXAMPLES)/parse.o
SEGPARSE_OBJS          := $(OBJ_EXAMPLES)/segparse.o
OS8104_CTL_OBJS        := $(OBJ_EXAMPLES)/os8104_ctl.o
OS8104_AINT_PROBE_OBJS := $(OBJ_EXAMPLES)/os8104_aint_probe.o
RESOURCE_ALLOC_OBJS    := $(OBJ_EXAMPLES)/resource_alloc.o
AUDIO_TX_OBJS          := $(OBJ_EXAMPLES)/audio_tx.o
AMP_PROBE_OBJS         := $(OBJ_EXAMPLES)/amp_probe.o
DEALLOCATE_OBJS        := $(OBJ_EXAMPLES)/deallocate.o
AUDIO_INJECT_OBJS      := $(OBJ_EXAMPLES)/audio_inject.o
WHOSE_CHANNEL_OBJS     := $(OBJ_EXAMPLES)/whose_channel.o
SOURCE_PROBE_OBJS      := $(OBJ_EXAMPLES)/source_probe.o

.DEFAULT_GOAL := all
.PHONY: all clean $(PROGRAMS)

all: $(addprefix $(DIST_BIN_DIR)/,$(PROGRAMS))

$(PROGRAMS): %: $(DIST_BIN_DIR)/%

$(LIBMOST_STATIC):
	$(MAKE) -C $(LIBMOST_DIR)

$(OBJ_EXAMPLES)/%.o: $(EXAMPLES_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/most-send: $(MOST_SEND_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(MOST_SEND_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/sniffer: $(SNIFFER_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SNIFFER_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/example: $(EXAMPLE_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(EXAMPLE_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/segtest: $(SEGTEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SEGTEST_OBJS) $(LDLIBS)

$(BIN_DIR)/parse: $(PARSE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(PARSE_OBJS) $(LDLIBS)

$(BIN_DIR)/segparse: $(SEGPARSE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SEGPARSE_OBJS) $(LDLIBS)

$(BIN_DIR)/os8104_ctl: $(OS8104_CTL_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OS8104_CTL_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/os8104_aint_probe: $(OS8104_AINT_PROBE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OS8104_AINT_PROBE_OBJS) $(LDLIBS)

$(BIN_DIR)/resource_alloc: $(RESOURCE_ALLOC_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(RESOURCE_ALLOC_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/audio_tx: $(AUDIO_TX_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(AUDIO_TX_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/amp_probe: $(AMP_PROBE_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(AMP_PROBE_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/deallocate: $(DEALLOCATE_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(DEALLOCATE_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/audio_inject: $(AUDIO_INJECT_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(AUDIO_INJECT_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(DIST_BIN_DIR)/%: $(BIN_DIR)/%
	@mkdir -p $(dir $@)
	cp -f $< $@

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR)

$(BIN_DIR)/whose_channel: $(WHOSE_CHANNEL_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(WHOSE_CHANNEL_OBJS) $(LIBMOST_STATIC) $(LDLIBS)

$(BIN_DIR)/source_probe: $(SOURCE_PROBE_OBJS) $(LIBMOST_STATIC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCE_PROBE_OBJS) $(LIBMOST_STATIC) $(LDLIBS)
