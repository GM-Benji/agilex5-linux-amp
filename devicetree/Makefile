CURRENT_DIR := $(CURDIR)
TARGET := socfpga_agilex5_de25_nano

all: $(TARGET).dtb

$(TARGET).dtb: $(TARGET).dts
	@echo "1. Preprocesor C (rozwiązywanie #include)..."
	cpp -nostdinc -I $(INCLUDE_DTS) -I $(INCLUDE_INC) -I $(CURRENT_DIR) -undef -x assembler-with-cpp $(TARGET).dts $(TARGET).dts.tmp
	
	@echo "2. Kompilacja dtc (rozwiązywanie /include/ i budowa dtb)..."
	dtc -I dts -O dtb -i $(INCLUDE_DTS) -i $(CURRENT_DIR) -o $(TARGET).dtb $(TARGET).dts.tmp
	
	@echo "3. Sprzątanie..."
	rm $(TARGET).dts.tmp

clean:
	rm -f *.dtb *.tmp
