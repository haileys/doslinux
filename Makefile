.PHONY: all clean

all: hdd.img

clean:
	rm -f hdd.img

hdd.img: hdd.base.img doslinux.com
	cp hdd.base.img hdd.img
	MTOOLSRC=mtoolsrc mmd C:/doslinux
	MTOOLSRC=mtoolsrc mcopy doslinux.com C:/doslinux/dsl.com
	MTOOLSRC=mtoolsrc mcopy linux-5.8.9/arch/x86/boot/bzImage C:/doslinux/bzimage

doslinux.com: doslinux.asm
	nasm -o $@ -f bin $<
