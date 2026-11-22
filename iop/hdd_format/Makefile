EXFAT ?= 1
IOP_BIN = hdd_format-$(EXFAT).irx
IOP_OBJS_DIR = obj-$(EXFAT)/
IOP_SRC_DIR = ./
IOP_OBJS = $(addprefix $(IOP_OBJS_DIR),main.o imports.o)
IOP_CFLAGS += -Wall -Wextra
IOP_INCS += -I. -I../../include
ifeq ($(EXFAT),1)
IOP_CFLAGS += -DEXFAT
endif
all: $(IOP_BIN)
clean:
	rm -f *.irx *.elf
	rm -rf obj-0 obj-1
include $(PS2SDK)/Defs.make
include ../Rules.make
$(IOP_OBJS): ../../include/hdd_format_rpc.h Makefile
$(IOP_OBJS): | $(IOP_OBJS_DIR)
