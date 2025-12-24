# kernel build

obj-m += src/yufs.c

PWD := $(CURDIR)
KDIR = /lib/modules/'uname -r'/build
EXTRA_CFLAGS = -Wall -g

all:
	make -C $(KDIR) <=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -rf .cache