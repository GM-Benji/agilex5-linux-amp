all: boot.scr

boot.scr: boot.txt
	mkimage -A arm -O linux -T script -C none -a 0 -e 0 -n "Automatyczny Start" -d boot.txt boot.scr

clean:
	rm -f boot.scr
