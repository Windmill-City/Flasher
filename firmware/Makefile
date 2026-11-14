# CH582F Flasher / Dongle programmer firmware

MRS_TOOLCHAIN ?= C:/MounRiver/MounRiver_Studio3/resources/app/resources/win32/components/WCH/Toolchain/RISC-V Embedded GCC15/bin
PREFIX        := $(MRS_TOOLCHAIN)/riscv32-wch-elf-
CXX           := "$(PREFIX)g++.exe"
OBJCOPY       := "$(PREFIX)objcopy.exe"
SIZE          := "$(PREFIX)size.exe"

TARGET        := flasher
BUILD         := build


ARCH          := -march=rv32imac -mabi=ilp32 -mcmodel=medany
CXXFLAGS      := $(ARCH) -Os -std=c++17 \
                 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
                 -fno-use-cxa-atexit -ffunction-sections -fdata-sections \
                 -fno-common -fsigned-char -fmessage-length=0 \
                 -Wall -Wextra
ASFLAGS       := $(ARCH) -x assembler-with-cpp
INCLUDES      := -Iinclude

APP_CXX       := src/main.cpp \
                 src/platform/clock.cpp \
                 src/usb/device.cpp src/usb/host.cpp \
                 src/serial/uart.cpp \
                 src/isp/isp.cpp \
                 src/app/app.cpp

LDSCRIPT      := ld/ch582.ld
LIBS          := -lm

OBJECTS       := $(patsubst %.cpp,$(BUILD)/%.o,$(APP_CXX)) \
                 $(BUILD)/src/platform/startup_ch582.o
DEPS          := $(OBJECTS:.o=.d)

LDFLAGS       := $(ARCH) -T$(LDSCRIPT) -nostartfiles \
                 -Wl,--gc-sections -Wl,--print-memory-usage \
                 -Wl,-Map=$(BUILD)/$(TARGET).map \
                 --specs=nano.specs --specs=nosys.specs


ifeq ($(OS),Windows_NT)
SHELL         := cmd.exe
MKDIR_P        = if not exist "$(subst /,\,$(patsubst %/,%,$(1)))" mkdir "$(subst /,\,$(patsubst %/,%,$(1)))"
RM_RF          = if exist "$(subst /,\,$(1))" rmdir /s /q "$(subst /,\,$(1))"
else
MKDIR_P        = mkdir -p "$(1)"
RM_RF          = rm -rf "$(1)"
endif

.PHONY: all build clean help

all: build

build: $(BUILD)/$(TARGET).hex

$(BUILD)/$(TARGET).elf: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)
	$(SIZE) $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@
	$(OBJCOPY) -O binary $< $(BUILD)/$(TARGET).bin
	@echo "built $@"

$(BUILD)/%.o: %.cpp
	@$(call MKDIR_P,$(dir $@))
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.S
	@$(call MKDIR_P,$(dir $@))
	$(CXX) $(ASFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@$(call RM_RF,build)

help:
	@echo "make             build the Flasher image at 0x00000000"
	@echo "make clean       remove build outputs"

-include $(DEPS)
