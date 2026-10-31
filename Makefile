.DEFAULT_GOAL := all

BUILD_ROOT ?= $(CURDIR)/build
DIST_ROOT  ?= $(CURDIR)/dist
export BUILD_ROOT DIST_ROOT

LIBMOST_DIR := libmost
TOOLS_DIR   := tools
MOSTD_DIR   := mostd
DRIVER_DIR  := driver

.PHONY: all libmost tools mostd driver clean

all: driver libmost mostd

libmost:
	$(MAKE) -C $(LIBMOST_DIR)

tools: libmost
	$(MAKE) -C $(TOOLS_DIR)

mostd: libmost
	$(MAKE) -C $(MOSTD_DIR)

driver:
	$(MAKE) -C $(DRIVER_DIR)

clean:
	$(MAKE) -C $(TOOLS_DIR) clean
	$(MAKE) -C $(LIBMOST_DIR) clean
	$(MAKE) -C $(MOSTD_DIR) clean
	-$(MAKE) -C $(DRIVER_DIR) clean
