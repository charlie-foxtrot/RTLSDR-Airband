export CC = g++
export CFLAGS = -O3
export CXXFLAGS = $(CFLAGS)
LDLIBS = -lrt -lm -lvorbisenc -lmp3lame -lshout -lpthread -lrtlsdr

SUBDIRS = hello_fft
CLEANDIRS = $(SUBDIRS:%=clean-%)

OBJ = rtl_airband.o
FFT = hello_fft/hello_fft.a

.PHONY: all clean $(SUBDIRS) $(CLEANDIRS)

all:
	@printf "\nPlease choose one of available platforms:\n \
	\tmake rtl_airband_vfp\t\tbuild binary for Raspberry Pi V1 (optimized for VFP coprocessor)\n \
	\tmake rtl_airband_neon\t\tbuild binary for Raspberry Pi V2 (optimized for NEON coprocessor)\n \
	\tmake rtl_airband_win\t\tbuild Windows binary (not tested, unmaintained)\n"

rtl_airband_vfp rtl_airband_neon: CFLAGS += -I/opt/vc/include  -I/opt/vc/include/interface/vcos/pthreads \
	-I/opt/vc/include/interface/vmcs_host/linux
	LDLIBS += -lbcm_host
	export LDFLAGS = -L/opt/vc/lib

rtl_airband_vfp: CFLAGS += -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk -mfpu=vfp
rtl_airband_vfp: $(OBJ) $(FFT) rtl_airband_vfp.o

rtl_airband_neon: CFLAGS += -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
rtl_airband_neon: $(OBJ) $(FFT) rtl_airband_neon.o

rtl_airband_win: $(OBJ)

$(FFT):	hello_fft

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(CLEANDIRS)
	rm -f *.o rtl_airband_neon rtl_airband_vfp

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

