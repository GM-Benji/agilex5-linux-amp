obj-m += wake_a55.o

KDIR := /home/benji/rtubuntuAgilex/linux-socfpga

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
