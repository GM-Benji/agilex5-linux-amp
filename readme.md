
```markdown
# Agilex 5 AMP System (Linux RT + Bare-Metal)

This repository contains the necessary components to configure and run an Asymmetric Multiprocessing (AMP) architecture on the Terasic DE25-Nano board (Agilex 5 SoC). 

The system runs a custom PREEMPT_RT Linux kernel on 3 HPS cores (1x A55, 2x A76) and dedicates the 4th core (A55) to executing bare-metal code. This repository manages the out-of-tree build process for the Device Tree, the U-Boot boot script, and the custom kernel module responsible for waking up the dedicated A55 core and managing zero-copy communication.

##  Repository Structure

* `linux-source/` - A Git submodule containing the Intel SoC FPGA Linux kernel (patched with PREEMPT_RT and configured for Agilex 5).
* `linux-driver/` - Contains the `wake_a55` kernel module for core management and sysfs interface.
* `devicetree/` - Contains the Device Tree Source (`.dts`) tailored for the AMP configuration.
* `boot/` - Contains the U-Boot startup script (`boot.txt`).

##  Setup for New Users

Because this project relies on a modified Linux kernel, it uses Git submodules. **Do not download this repository as a ZIP file.** To clone the project along with the kernel source, use the `--recursive` flag:

```bash
git clone --recursive [https://github.com/GM-Benji/agilex5-linux-amp.git](https://github.com/GM-Benji/agilex5-linux-amp.git)
cd agilex5-linux-amp
```

*(If you already cloned it without the flag, run `git submodule update --init --recursive` inside the folder).*

##  Build Instructions

### 1. Build the Kernel First
Before building the AMP tools (driver, dtb, etc.), you must compile the Linux kernel inside the submodule. The top-level Makefile depends on the compiled kernel headers.

```bash
cd linux-source
# Load the configuration (replace with your specific config if needed)
make socfpga_defconfig  
# Compile the kernel, modules, and device trees
make -j$(nproc) Image modules dtbs
cd ..
```

### 2. Build the AMP Components
Once the kernel is built, return to the root directory of this repository and run:

```bash
make
```

This will automatically generate:
* `linux-driver/wake_a55.ko`
* `devicetree/socfpga_agilex5_de25_nano.dtb`
* `boot/boot.scr`

##  SD Card Deployment

After a successful build, copy the generated files to your SD card. The SD card should have at least two partitions: a FAT32 boot partition and an ext4 rootfs partition.

### 1. Boot Partition (FAT32)
Copy the Device Tree Blob and the U-Boot script into the `/boot` directory of the FAT32 partition:
* `boot.scr`  -> `[FAT32_PARTITION]/boot/`
* `socfpga_agilex5_de25_nano.dtb` -> `[FAT32_PARTITION]/boot/`
* *(You should also copy the `Image` file from `linux-source/arch/arm64/boot/Image` here).*

### 2. Root Filesystem (ext4)
Copy the compiled kernel module and your bare-metal binary to the user directory on the Linux filesystem (e.g., `/home/terasic`):
* `wake_a55.ko` -> `[EXT4_PARTITION]/home/terasic/`
* `baremetal_a55.bin` -> `[EXT4_PARTITION]/home/terasic/` *(Note: This binary is compiled separately)*
* repository with bare metal code: https://github.com/GM-Benji/agilex-bare-metal

---

##  Usage & Execution

Once the board boots into Linux, navigate to the `/home/terasic` directory and use the following commands to manage the bare-metal core.

### 1. Driver Management

**Load the driver:**
```bash
sudo insmod wake_a55.ko
```

**Check driver initialization logs:**
```bash
dmesg | tail -n 8
```

**Unload the driver (if needed):**
```bash
sudo rmmod wake_a55 2>/dev/null
```

### 2. Firmware Loading & Execution

The driver exposes a `sysfs` interface under `/sys/kernel/baremetal/` to interact with the 4th A55 core.

**Load the bare-metal binary into the dedicated memory:**
```bash
sudo sh -c 'cat baremetal_a55.bin > /sys/kernel/baremetal/fw'
```

**Wake up the A55 core and start execution:**
```bash
sudo sh -c 'echo 1 > /sys/kernel/baremetal/wake'
```

### 3. AMP Communication (Zero-Copy)

Once the core is running, you can communicate with the bare-metal application using the custom zero-copy IPC interface.

**Send a command to the bare-metal core:**
```bash
sudo sh -c 'echo "HELLO_AGILEX" > /sys/kernel/baremetal/cmd'
```

**Read the response/logs from the bare-metal core:**
```bash
cat /sys/kernel/baremetal/log
```
```

