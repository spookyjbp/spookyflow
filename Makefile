#---------------------------------------------------------------------------------
# spookyflow - PSL1GHT / Tiny3D build.
# Structure mirrors wargio/tiny3D samples/sprites2D/Makefile (standard PSL1GHT
# template). Package variables are set BEFORE including ppu_rules because
# ppu_rules assigns them with ?= defaults.
#   make        -> spookyflow.self / .elf
#   make pkg    -> spookyflow.pkg (+ spookyflow.gnpdrm.pkg)
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

TITLE     := spookyflow
APPID     := SPKF00001
CONTENTID := UP0001-$(APPID)_00-0000000000000000
ICON0     := $(CURDIR)/pkgfiles/ICON0.PNG
SFOXML    := $(CURDIR)/sfo.xml

include $(PSL1GHT)/ppu_rules

#---------------------------------------------------------------------------------
TARGET   := spookyflow
BUILD    := build
SOURCES  := src/core src/ps3
DATA     :=
INCLUDES := src/core src/ps3
PKGFILES := $(CURDIR)/pkgfiles

SCETOOL_FLAGS +=

#---------------------------------------------------------------------------------
# libraries: same set as the Tiny3D sprites2D sample, plus libsysfs for
# sysFsOpendir/Readdir/Stat (Apollo Save Tool links it the same way).
LIBS := -ltiny3d -lrsx -lsimdmath -lgcm_sys -lio -lsysutil -laudio -lnet -lrt -lsysfs -llv2 -lpngdec -ljpgdec -lsysmodule -lm

CFLAGS   = -O2 -Wall -std=gnu99 -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS = $(CFLAGS)
LDFLAGS  = $(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBDIRS :=

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)
export BUILDDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES := $(addsuffix .o,$(BINFILES)) \
                 $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
                 $(sFILES:.s=.o) $(SFILES:.S=.o)

export INCLUDE := $(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  $(LIBPSL1GHT_INC) \
                  -I$(CURDIR)/$(BUILD) -I$(PORTLIBS)/include

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
                   $(LIBPSL1GHT_LIB) -L$(PORTLIBS)/lib

.PHONY: $(BUILD) clean pkg run

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).fake.self $(OUTPUT)*.pkg

run:
	ps3load $(OUTPUT).self

pkg: $(BUILD) $(OUTPUT).pkg

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
#---------------------------------------------------------------------------------
