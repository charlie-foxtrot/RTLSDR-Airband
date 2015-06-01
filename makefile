export CC = g++
export CFLAGS = -O3 -g
export CXXFLAGS = $(CFLAGS)
LDLIBS = -lrt -lm -lvorbisenc -lmp3lame -lshout -lpthread -lrtlsdr -lconfig++

SUBDIRS = hello_fft
CLEANDIRS = $(SUBDIRS:%=clean-%)

OBJ = rtl_airband.o
FFT = hello_fft/hello_fft.a

.PHONY: all clean $(SUBDIRS) $(CLEANDIRS)

ifeq ($(PLATFORM), rpiv1)
  CFLAGS += -I/opt/vc/include  -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
  CFLAGS += -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk -mfpu=vfp
  LDLIBS += -lbcm_host
  export LDFLAGS = -L/opt/vc/lib
  DEPS = $(OBJ) $(FFT) rtl_airband_vfp.o
else ifeq ($(PLATFORM), rpiv2)
  CFLAGS += -I/opt/vc/include  -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
  CFLAGS += -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
  LDLIBS += -lbcm_host
  export LDFLAGS = -L/opt/vc/lib
  DEPS = $(OBJ) $(FFT) rtl_airband_neon.o
else ifeq ($(PLATFORM), x86)
  CFLAGS += -march=native
  LDLIBS += -lfftw3f
  DEPS = $(OBJ)
else
  DEPS =
endif

all:
ifndef DEPS
	@printf "\nPlease set PLATFORM variable to one of available platforms:\n \
	\tPLATFORM=rpiv1 make\t\tbuild binary for Raspberry Pi V1 (optimized for VFP coprocessor)\n \
	\tPLATFORM=rpiv2 make\t\tbuild binary for Raspberry Pi V2 (optimized for NEON coprocessor)\n \
	\tPLATFORM=x86 make\t\tbuild binary for x86\n\n"
	@false
endif
	$(MAKE) rtl_airband

rtl_airband: $(DEPS)

$(FFT):	hello_fft

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(CLEANDIRS)
	rm -f *.o rtl_airband

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

