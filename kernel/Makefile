# Out-of-tree build of snd-vban:  make            (running kernel)
#                                  make KDIR=/path/to/kernel/build
obj-m := snd-vban.o
snd-vban-y := snd_vban.o shim/vban_proto_k.o
ccflags-y := -I$(src)/shim -I$(src)/../src -DVBAN_ENABLE_NET=0 -DVBAN_ENABLE_F64=0 -Wno-declaration-after-statement

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
