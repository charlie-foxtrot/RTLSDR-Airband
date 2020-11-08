# Install prefix
PREFIX = /usr/local

SYSCONFDIR = $(PREFIX)/etc
DEFCONFIG = config/basic_multichannel.conf
CFG = rtl_airband.conf
BINDIR = $(PREFIX)/bin
export DEBUG ?= 0
export WITH_RTLSDR ?= 1
export CC = g++
export CFLAGS = -O3 -g -Wall -Wextra -DSYSCONFDIR=\"$(SYSCONFDIR)\" -DDEBUG=$(DEBUG)
RTL_AIRBAND_VERSION:=\"$(shell git describe --always --tags --dirty 2>/dev/null)\"
ifneq ($(RTL_AIRBAND_VERSION), \"\")
  CFLAGS+=-DRTL_AIRBAND_VERSION=$(RTL_AIRBAND_VERSION)
endif
export CXXFLAGS = $(CFLAGS)
export LDFLAGS = -rdynamic
LDLIBS = -lrt -lm -ldl -lvorbisenc -lmp3lame -lshout -lpthread -lconfig++
INSTALL_USER = root
INSTALL_GROUP = root

SUBDIRS = hello_fft
CLEANDIRS = $(SUBDIRS:%=clean-%)

BIN = rtl_airband
OBJ = rtl_airband.o input-common.o input-helpers.o input-file.o output.o config.o util.o mixer.o
FFT = hello_fft/hello_fft.a

.PHONY: all clean install help $(SUBDIRS) $(CLEANDIRS)

UNKNOWN_PLATFORM = 0
ifeq ($(PLATFORM), rpiv1)
  CFLAGS += -DUSE_BCM_VC
  CFLAGS += -I/opt/vc/include  -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
  CFLAGS += -mcpu=arm1176jzf-s -mtune=arm1176jzf-s -march=armv6zk -mfpu=vfp -ffast-math 
  LDLIBS += -lbcm_host -ldl
  LDFLAGS += -L/opt/vc/lib
  DEPS = $(OBJ) $(FFT) rtl_airband_vfp.o
else ifeq ($(PLATFORM), rpiv2)
  CFLAGS += -DUSE_BCM_VC
  CFLAGS += -I/opt/vc/include  -I/opt/vc/include/interface/vcos/pthreads -I/opt/vc/include/interface/vmcs_host/linux
  CFLAGS += -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math 
  LDLIBS += -lbcm_host -ldl
  LDFLAGS += -L/opt/vc/lib
  DEPS = $(OBJ) $(FFT) rtl_airband_neon.o
else ifeq ($(PLATFORM), armv7-generic)
  CFLAGS += -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -ffast-math 
  LDLIBS += -lfftw3f
  DEPS = $(OBJ)
else ifeq ($(PLATFORM), armv8-generic)
  CFLAGS += -march=armv8-a+crc -mtune=cortex-a53 -ffast-math
  LDLIBS += -lfftw3f
  DEPS = $(OBJ)
else ifeq ($(PLATFORM), x86)
  CFLAGS += -march=native
  LDLIBS += -lfftw3f
  DEPS = $(OBJ)
else ifeq ($(PLATFORM), x86-freebsd)
  CFLAGS += -march=native -I/usr/local/include
  LDLIBS += -lfftw3f -lc++
  DEPS = $(OBJ)
  INSTALL_GROUP = wheel
else
  UNKNOWN_PLATFORM = 1
endif
ifeq ($(NFM), 1)
  CFLAGS += -DNFM
endif

ifeq ($(PULSE), 1)
  CFLAGS += -DPULSE
  LDLIBS += -lpulse
  DEPS += pulse.o
endif

ifeq ($(WITH_RTLSDR), 1)
  CFLAGS += -DWITH_RTLSDR
  DEPS += input-rtlsdr.o
  LDLIBS += -lrtlsdr
endif

ifeq ($(WITH_MIRISDR), 1)
  CFLAGS += -DWITH_MIRISDR
  DEPS += input-mirisdr.o
  LDLIBS += -lmirisdr
endif

ifeq ($(WITH_SOAPYSDR), 1)
  CFLAGS += -DWITH_SOAPYSDR
  DEPS += input-soapysdr.o
  LDLIBS += -lSoapySDR
endif

ifeq ($(WITH_PROFILING), 1)
  CFLAGS += -DWITH_PROFILING
  LDLIBS += -lprofiler
endif

ifeq ($(UNKNOWN_PLATFORM),1)
  DEPS =
endif

$(BIN): $(DEPS)
ifndef DEPS
	$(MAKE) help
endif

help:
	@printf "\nPlease set PLATFORM variable to one of available platforms:\n \
	\tPLATFORM=rpiv1 make\t\tRaspberry Pi V1 (VFP FPU, use BCM VideoCore for FFT)\n \
	\tPLATFORM=rpiv2 make\t\tRaspberry Pi V2 (NEON FPU, use BCM VideoCore for FFT)\n \
	\tPLATFORM=armv7-generic make\tOther ARMv7 platforms, like Cubieboard (NEON FPU, use main CPU for FFT)\n \
	\tPLATFORM=armv8-generic make\t64-bit ARM platforms, like Odroid C2 (use main CPU for FFT)\n \
	\tPLATFORM=x86 make\t\tbuild binary for x86 (Linux)\n \
	\tPLATFORM=x86-freebsd gmake\tbuild binary for x86 (FreeBSD)\n\n \
	SDR Hardware options:\n \
	\tWITH_MIRISDR=1\t\t\tEnable Mirics DVB-T chipset support (via libmirisdr)\n \
	\tWITH_SOAPYSDR=1\t\t\tEnable SoapySDR support\n \
	Additional options:\n \
	\tNFM=1\t\t\t\tInclude support for Narrow FM demodulation\n \
	\t\t\t\t\tWarning: this incurs noticeable performance penalty both for AM and FM\n \
	\t\t\t\t\tDo not enable NFM, if you only use AM (especially on low-power platforms, like RPi)\n \
	\tPULSE=1\t\t\t\tInclude support for streaming to PulseAudio server\n\n"

$(FFT):	hello_fft ;

config.o: rtl_airband.h input-common.h

input-common.o: input-common.h

input-helpers.o: rtl_airband.h input-common.h

input-mirisdr.o: rtl_airband.h input-common.h input-helpers.h input-mirisdr.h

input-rtlsdr.o: rtl_airband.h input-common.h input-helpers.h input-rtlsdr.h

input-soapysdr.o: rtl_airband.h input-common.h input-helpers.h input-soapysdr.h

input-file.o: rtl_airband.h input-common.h input-helpers.h input-file.h

mixer.o: rtl_airband.h

rtl_airband.o: rtl_airband.h input-common.h

output.o: rtl_airband.h input-common.h

pulse.o: rtl_airband.h

util.o: rtl_airband.h

$(SUBDIRS):
	$(MAKE) -C $@

clean: $(CLEANDIRS)
	rm -f *.o rtl_airband

install: $(BIN)
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) $(BINDIR)
	install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 755 $(BIN) $(BINDIR)
	install -d -o $(INSTALL_USER) -g $(INSTALL_GROUP) $(SYSCONFDIR)
	test -f $(SYSCONFDIR)/$(CFG) || install -o $(INSTALL_USER) -g $(INSTALL_GROUP) -m 600 $(DEFCONFIG) $(SYSCONFDIR)/$(CFG)
	@printf "\n *** Done. If this is a new install, edit $(SYSCONFDIR)/$(CFG) to suit your needs.\n\n"

$(CLEANDIRS):
	$(MAKE) -C $(@:clean-%=%) clean

