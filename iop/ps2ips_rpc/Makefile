IOP_BIN = ../ps2ips_rpc.irx
IOP_OBJS = ps2ips.o imports.o
IOP_CFLAGS += -DPS2IP_DNS -Wall
IOP_OPTFLAGS = -O2
all: $(IOP_BIN)
clean:
	rm -f $(IOP_BIN) $(IOP_OBJS)
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
$(IOP_OBJS): $(wildcard *.h) Makefile
