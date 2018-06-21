
.PHONY: all clean

SRC = apps/micropython/src
GENHDR = apps/micropython/src/genhdr

INC += -I$(SRC)
INC += -Irepos/apache-mynewt-core/hw/cmsis-core/src/ext
INC += -Irepos/apache-mynewt-core/hw/drivers/uart/include
INC += -Irepos/apache-mynewt-core/hw/hal/include
INC += -Irepos/apache-mynewt-core/hw/mcu/nordic/nrf52xxx/include
INC += -Irepos/apache-mynewt-core/hw/mcu/nordic/src/ext/nrfx/mdk
INC += -Irepos/apache-mynewt-core/kernel/os/include
INC += -Irepos/apache-mynewt-core/kernel/os/include/os/arch/cortex_m4
INC += -Irepos/apache-mynewt-core/sys/console/minimal/include
INC += -Irepos/apache-mynewt-core/sys/defs/include
INC += -Irepos/apache-mynewt-core/sys/flash_map/include
INC += -Irepos/apache-mynewt-nimble/porting/nimble/include
INC += -Ibin/targets/pca10040/generated/include

CFLAGS += $(INC)
CFLAGS += -DNRF52832_XXAA
CLFAGS += -Wall -Werror -std=c99

all: $(GENHDR)/mpversion.h $(GENHDR)/qstrdefs.generated.h
	newt build pca10040
	newt create-image pca10040 1.0.0

clean:
	newt clean pca10040
	rm -rf $(GENHDR)

$(GENHDR)/mpversion.h:
	@mkdir -p $(GENHDR)
	python ../../py/makeversionhdr.py $(GENHDR)/mpversion.h

$(GENHDR)/qstr.i.last:
	@mkdir -p $(GENHDR)
	arm-none-eabi-gcc -E -DNO_QSTR $(CFLAGS) -mthumb -mtune=cortex-m4 -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fsingle-precision-constant -Wdouble-promotion -Os -DNDEBUG $(SRC)/*.c >$(GENHDR)/qstr.i.last

$(GENHDR)/qstr.split: $(GENHDR)/qstr.i.last
	python ../../py/makeqstrdefs.py split $(GENHDR)/qstr.i.last $(GENHDR)/qstr $(GENHDR)/qstrdefs.collected.h
	touch $(GENHDR)/qstr.split

$(GENHDR)/qstrdefs.collected.h: $(GENHDR)/qstr.split
	python ../../py/makeqstrdefs.py cat $(GENHDR)/qstr.i.last $(GENHDR)/qstr $(GENHDR)/qstrdefs.collected.h

$(GENHDR)/qstrdefs.generated.h: $(GENHDR)/qstrdefs.collected.h
	cat ../../py/qstrdefs.h $(GENHDR)/qstrdefs.collected.h | sed 's/^Q(.*)/"&"/' | arm-none-eabi-gcc -E $(CFLAGS) -nostdlib -mthumb -mtune=cortex-m4 -mabi=aapcs-linux -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fsingle-precision-constant -Wdouble-promotion  -Os -DNDEBUG - | sed 's/^"\(Q(.*)\)"/\1/' > $(GENHDR)/qstrdefs.preprocessed.h
	python ../../py/makeqstrdata.py $(GENHDR)/qstrdefs.preprocessed.h >$(GENHDR)/qstrdefs.generated.h
