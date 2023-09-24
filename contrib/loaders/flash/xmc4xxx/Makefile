# SPDX-License-Identifier: GPL-2.0-or-later
# export CROSS_COMPILE=/opt/gcc-arm-none-eabi/bin/arm-none-eabi-

BIN2C = ../../../../src/helper/bin2char.sh

CROSS_COMPILE ?= arm-none-eabi-

CC=$(CROSS_COMPILE)gcc
OBJCOPY=$(CROSS_COMPILE)objcopy
OBJDUMP=$(CROSS_COMPILE)objdump

CFLAGS = -c -mthumb -mcpu=cortex-m4 -nostdlib -nostartfiles -Os -g -fPIC

all: write.inc

.PHONY: clean

.INTERMEDIATE: write.elf

%.elf: %.c
	$(CC) $(CFLAGS) $< -o $@

%.lst: %.elf
	$(OBJDUMP) -S $< > $@

%.bin: %.elf
	$(OBJCOPY) -Obinary $< $@

%.inc: %.bin
	$(BIN2C) < $< > $@

clean:
	-rm -f *.elf *.lst *.bin *.inc
