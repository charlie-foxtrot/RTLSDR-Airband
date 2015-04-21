export CC = g++
export CFLAGS = -O3 -I/opt/vc/include  -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
export CFLAGS += -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk -mfpu=vfp
export CXXFLAGS = $(CFLAGS)
export LDFLAGS = -L/opt/vc/lib
LDLIBS = -lrt -lm -lbcm_host -lvorbisenc -lmp3lame -lshout -lpthread -lrtlsdr

SUBDIRS = hello_fft
CLEANDIRS = $(SUBDIRS:%=clean-%)

OBJ = hello_fft/hello_fft.a rtl_airband.o rtl_airband_asm.o
B = rtl_airband

.PHONY: all clean $(SUBDIRS) $(CLEANDIRS)

all: $(SUBDIRS) $(B)

$(SUBDIRS):
	$(MAKE) -C $@

$(B): $(OBJ)

clean: $(CLEANDIRS)
	rm -f *.o $(B)

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

