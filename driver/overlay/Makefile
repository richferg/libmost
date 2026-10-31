# ===== overlay/Makefile =====

DTC := dtc
SRC := $(wildcard *.dts)
DTBO := $(SRC:.dts=.dtbo)

.DEFAULT_GOAL := all
.PHONY: all clean

all: $(DTBO)

%.dtbo: %.dts
	@echo "Building overlay: $@"
	$(DTC) -@ -I dts -O dtb -o $@ $<

clean:
	rm -f *.dtbo
