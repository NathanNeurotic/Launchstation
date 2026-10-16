IOP_BIN = ../pccf.irx
IOP_OBJS = main.o pccf_hw.o pccf_ata.o pcdiag_cis.o pcdiag_cf.o imports.o
IOP_CFLAGS += -DPCCF_SETTLE_US=1000 -fno-strict-aliasing
IOP_OPTFLAGS = -O2
all: $(IOP_BIN)
clean:
	rm -f $(IOP_BIN) $(IOP_OBJS)
include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
$(IOP_OBJS): $(wildcard *.h *.inc) Makefile
