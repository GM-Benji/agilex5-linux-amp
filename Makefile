# comment if you are using ARM CPU
export ARCH ?= arm64
export CROSS_COMPILE ?= aarch64-linux-gnu-

export KDIR := $(CURDIR)/linux-source


export INCLUDE_DTS := $(KDIR)/arch/arm64/boot/dts/intel
export INCLUDE_INC := $(KDIR)/include

SUBDIRS := devicetree linux-driver boot

.PHONY: all clean $(SUBDIRS)

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
