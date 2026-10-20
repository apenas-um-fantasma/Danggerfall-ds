# Makefile - Daggerfall DS (libnds / devkitARM)
# Uso:  make          -> gera daggerfall_ds.nds
#       make clean
# Requer devkitPro com o grupo "nds-dev" (libnds + ndstool). Variaveis DEVKITPRO/DEVKITARM definidas.
# OBS: se este Makefile falhar na sua versao da libnds, copie o Makefile do template oficial
#      ($DEVKITPRO/examples/nds/templates/arm9/Makefile) para esta pasta: ele aceita source/*.c direto.
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error Defina DEVKITPRO, ex.: export DEVKITPRO=/opt/devkitpro)
endif
DEVKITARM ?= $(DEVKITPRO)/devkitARM
export PATH := $(DEVKITARM)/bin:$(DEVKITPRO)/tools/bin:$(PATH)

TARGET  := daggerfall_ds
BUILD   := build
SRCS    := $(wildcard source/*.c)
OBJS    := $(patsubst source/%.c,$(BUILD)/%.o,$(SRCS))

LIBNDS  := $(DEVKITPRO)/libnds
CALICO  := $(DEVKITPRO)/calico
ARCH    := -march=armv5te -mtune=arm946e-s

ifneq ($(wildcard $(CALICO)/share/ds9.specs),)
  # libnds 2.x (calico)
  SPECS   := -specs=$(CALICO)/share/ds9.specs
  ARM7ELF := $(CALICO)/bin/ds7_maine.elf
  INCLUDE := -I$(LIBNDS)/include -I$(CALICO)/include
  LIBDIRS := -L$(LIBNDS)/lib -L$(CALICO)/lib
else
  # libnds 1.x
  SPECS   := -specs=ds_arm9.specs
  ARM7ELF := $(LIBNDS)/default.elf
  INCLUDE := -I$(LIBNDS)/include
  LIBDIRS := -L$(LIBNDS)/lib
endif

CFLAGS  := -g -Wall -O2 $(ARCH) -fomit-frame-pointer -ffunction-sections -fdata-sections -std=gnu11 -DARM9 $(INCLUDE)
LDFLAGS := $(SPECS) -g $(ARCH) -Wl,--gc-sections -Wl,-Map,$(BUILD)/$(TARGET).map $(LIBDIRS)
LIBS    := -lnds9 -lcalico_ds9

all: $(TARGET).nds

$(TARGET).nds: $(TARGET).elf
	ndstool -c $@ -9 $< -7 $(ARM7ELF) -b $(CURDIR)/icon.bmp "Daggerfall DS;Teste tecnico 3D;Mundo procedural"

$(TARGET).elf: $(OBJS)
	arm-none-eabi-gcc $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(BUILD)/%.o: source/%.c | $(BUILD)
	arm-none-eabi-gcc $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nds

.PHONY: all clean
