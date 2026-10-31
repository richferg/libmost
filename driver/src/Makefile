# SPDX-License-Identifier: GPL-2.0

# When called from the kernel build system, just list the objects.
ifneq ($(KERNELRELEASE),)
obj-m := os8104.o
os8104-objs := \
	os8104_driver.o \
	os8104_hw.o \
	os8104_link.o

# Otherwise support building the module directly from this directory.
else
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

.DEFAULT_GOAL := all
.PHONY: all clean

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
endif
