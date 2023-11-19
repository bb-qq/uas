#
#
#

VERSION := $(shell grep Linux/ /usr/local/sysroot/usr/include/linux/syno_autoconf.h | cut -d " " -f 4 | cut -d "." -f 1-2)

PLATFORM := $(shell echo $(SYNO_PLATFORM) | tr A-Z a-z)
ROOT := linux-$(PLATFORM)/drivers/usb/storage
CHROOT := /source/uas/linux-$(PLATFORM)
TARGETS := $(ROOT)/usb-storage.ko $(ROOT)/uas.ko

.PHONY: all
all: modules spk_su

.PHONY: modules
modules: $(ROOT)/uas.c
	$(MAKE) -C $(KSRC) \
		M=$(CHROOT)/drivers/usb/storage \
		EXTRA_CFLAGS="-I$(CHROOT)/include -DCONFIG_USB_UAS" \
		CONFIG_USB_UAS=m \
		modules 

$(ROOT)/uas.c: /tmp/linux.txz
	mkdir -p $(CHROOT)
	tar -xJf $(<) -C $(CHROOT) --strip-components=1 --wildcards --no-anchored '*/drivers/usb/storage' '*/drivers/scsi/sd.h' '*/include/linux/usb/syno_quirks.h'

/tmp/linux.txz:
	curl -k -R -o $(@) https://global.synologydownload.com/download/ToolChain/Synology%20NAS%20GPL%20Source/7.2-64570/$(PLATFORM)/linux-4.4.x.txz

spk_su: spk_su.c
	$(CROSS_COMPILE)cc -std=c99 -o $(@) $(<)

.PHONY: clean
clean:
	rm -rf *.o $(TARGET) spk_su

.PHONY: install
install: $(TARGETS) spk_su
	mkdir -p $(DESTDIR)/uas/
	install $(^) $(DESTDIR)/uas/

