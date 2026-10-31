KDIR ?= /lib/modules/$(shell uname -r)/build
INSTALL ?= install
DEPMOD ?= depmod

MODULE_DIR  := src
OVERLAY_DIR := overlay
MODULE      := os8104
OVERLAYS    := os8104 pimost

MOD_DEST     := /lib/modules/$(shell uname -r)/extra
OVERLAY_DEST := /boot/overlays

.DEFAULT_GOAL := all
.PHONY: all module overlay clean debug release run run-debug install

all: module overlay

release: all

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MODULE_DIR) modules

overlay:
	$(MAKE) -C $(OVERLAY_DIR)

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MODULE_DIR) clean
	$(MAKE) -C $(OVERLAY_DIR) clean

debug:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/$(MODULE_DIR) KCFLAGS=-DDEBUG modules
	$(MAKE) -C $(OVERLAY_DIR)

run: all
	./run.sh

run-debug: debug
	./run.sh

install: all
	$(INSTALL) -d $(MOD_DEST)
	$(INSTALL) -m 644 $(MODULE_DIR)/$(MODULE).ko $(MOD_DEST)/
	$(DEPMOD) -a
	$(INSTALL) -d $(OVERLAY_DEST)
	$(INSTALL) -m 644 $(addprefix $(OVERLAY_DIR)/,$(addsuffix .dtbo,$(OVERLAYS))) $(OVERLAY_DEST)/
