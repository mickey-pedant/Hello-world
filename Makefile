obj-m := hadm_kmod.o

hadm_kmod-objs := miniblk.o
hadm_kmod-objs += syncer.o
hadm_kmod-objs += bio_helper.o
hadm_kmod-objs += buffer.o

BUILD_DIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(BUILD_DIR) M=$(PWD) modules

clean:
	make -C $(BUILD_DIR) M=$(PWD) clean
