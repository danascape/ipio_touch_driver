# include header files for MTK
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/ipio_touch_driver/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/

ccflags-y += -Wall

## Built-in
#obj-y += core/
#obj-y += platform.o userspace.o

## Module
obj-m += ilitek.o
ilitek-y := platform.o userspace.o
ilitek-y += core/config.o \
			core/finger_report.o \
			core/firmware.o \
			core/flash.o \
			core/i2c.o \
			core/mp_test.o \
			core/protocol.o 

KERNEL_DIR= /home/likewise-open/ILI/1061279/workplace/rk3288_sdk/kernel
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
