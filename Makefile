# comment if you are using ARM CPU
export ARCH ?= arm64
export CROSS_COMPILE ?= aarch64-linux-gnu-

export KDIR := $(CURDIR)/linux-source

export INCLUDE_DTS := $(KDIR)/arch/arm64/boot/dts/intel
export INCLUDE_INC := $(KDIR)/include

# --- Bootloader Build Variables ---
UBOOT_DIR := u-boot-source
ATF_DIR := atf-source
HW_DIR := hardware
# Change this if Terasic provides a specific defconfig in configs/
UBOOT_DEFCONFIG := socfpga_agilex5_defconfig
# The board directory where the handoff file must be injected
UBOOT_BOARD_DIR := $(UBOOT_DIR)/board/intel/agilex5-socdk

# --- Existing AMP Subdirectories ---
SUBDIRS := devicetree linux-driver boot

.PHONY: all clean $(SUBDIRS) atf uboot clean-uboot

# Default target: builds only the Linux/AMP components
all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

# --- Bootloader Targets ---
atf:
	@echo "=== Building ARM Trusted Firmware (ATF) ==="
	$(MAKE) -C $(ATF_DIR) PLAT=agilex5 bl31
	@echo "Copying bl31.bin to U-Boot directory..."
	cp $(ATF_DIR)/build/agilex5/release/bl31.bin $(UBOOT_DIR)/

uboot: atf
	@echo "=== Injecting FPGA Hardware Handoff ==="
	mkdir -p $(UBOOT_BOARD_DIR)/qts/
	cp $(HW_DIR)/hps_bootloader_handoff.bin $(UBOOT_BOARD_DIR)/qts/
	@echo "=== Building U-Boot ==="
	$(MAKE) -C $(UBOOT_DIR) $(UBOOT_DEFCONFIG)
	$(MAKE) -C $(UBOOT_DIR) -j$$(nproc)
	@echo "=== Build Complete ==="
	@echo "-> Phase 1 (QSPI): $(UBOOT_DIR)/spl/u-boot-spl-dtb.hex"
	@echo "-> Phase 2 (SD): $(UBOOT_DIR)/u-boot.itb"

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done

clean-uboot:
	@echo "=== Cleaning Bootloader Environments ==="
	$(MAKE) -C $(ATF_DIR) realclean
	$(MAKE) -C $(UBOOT_DIR) distclean
