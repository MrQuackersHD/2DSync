#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET          := 2DSync
BUILD           := build
SOURCES         := source ../common
DATA            := data
INCLUDES        := include ../common
ROMFS           := romfs
APP_TITLE       := 2DSync
APP_DESCRIPTION := Your saves. Your consoles. Anywhere.
APP_AUTHOR      := 2DSync
ICON            := icon.png

ARCH := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS := -g -Wall -O2 -mword-relocations \
          -ffunction-sections \
          $(ARCH) $(INCLUDE) -D__3DS__
CFLAGS += -Wno-unused-function

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS := -g $(ARCH)
LDFLAGS = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh --romfs=$(CURDIR)/$(ROMFS)

.PHONY: all clean cia

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@if command -v bannertool >/dev/null 2>&1 && command -v makerom >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile cia; \
	else \
		echo "Built $(TARGET).3dsx. bannertool and makerom are required for $(TARGET).cia."; \
	fi

$(BUILD):
	@mkdir -p $@

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia banner.bnr

else

$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf : $(OFILES)

.PHONY: cia
cia: $(OUTPUT).cia

$(TOPDIR)/banner.bnr: $(TOPDIR)/banner.png $(TOPDIR)/banner.wav
	bannertool makebanner -i $(TOPDIR)/banner.png -a $(TOPDIR)/banner.wav -o $@

$(OUTPUT).cia: $(OUTPUT).elf $(OUTPUT).smdh $(TOPDIR)/banner.bnr
	makerom -f cia -o $@ -elf $(OUTPUT).elf -rsf $(TOPDIR)/app.rsf -target t -exefslogo -icon $(OUTPUT).smdh -banner $(TOPDIR)/banner.bnr

%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

endif
