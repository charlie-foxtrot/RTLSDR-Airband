# build container
FROM debian:bookworm-slim AS build

# install build dependencies
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      libmp3lame-dev \
      libshout3-dev \
      libconfig++-dev \
      libfftw3-dev \
      libsoapysdr-dev \
      libpulse-dev \
      \
      git \
      ca-certificates \
      libusb-1.0-0-dev \
      debhelper \
      pkg-config \
      && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*
  
# set working dir for building rtl-sdr support
WORKDIR /rtl_sdr_build
 
# compile / install rtl-sdr-blog version of rtl-sdr for v4 support
RUN git clone https://github.com/rtlsdrblog/rtl-sdr-blog && \
    cd rtl-sdr-blog/ && \
    dpkg-buildpackage -b --no-sign && \
    cd .. && \
    dpkg -i librtlsdr0_*.deb && \
    dpkg -i librtlsdr-dev_*.deb && \
    dpkg -i rtl-sdr_*.deb


# TODO: build anything from source?

# set working dir for project build
WORKDIR /rtl_airband_build

# copy in the rtl_airband source
# WARNING: not copying in the whole repo, this may need to be updated if build files are added outside of src/
COPY ./.git/ .git/
COPY ./src/ src/
COPY ./scripts/ scripts/
COPY ./CMakeLists.txt .

# configure and build
# TODO: detect platforms
RUN uname -m && \
    echo | gcc -### -v -E - | tee compiler_native_info.txt && \
    cmake -B build_dir -DPLATFORM=generic -DCMAKE_BUILD_TYPE=Release -DNFM=TRUE -DBUILD_UNITTESTS=TRUE && \
    VERBOSE=1 cmake --build build_dir -j4

# make sure unit tests pass
RUN ./build_dir/src/unittests


# application container
FROM debian:bookworm-slim

# install runtime dependencies
RUN apt-get update && \
  apt-get upgrade -y && \
  apt-get install -y --no-install-recommends \
    tini \
    libc6 \
    libmp3lame0 \
    libshout3 \
    libconfig++9v5 \
    libfftw3-single3 \
    libsoapysdr0.8 \
    libpulse0 \
    libusb-1.0-0-dev \
    && \
  apt-get clean && \
  rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# install (from build container) rtl-sdr-blog version of rtl-sdr for v4 support
COPY --from=build /rtl_sdr_build/librtlsdr0_*.deb /rtl_sdr_build/librtlsdr-dev_*.deb /rtl_sdr_build/rtl-sdr_*.deb /tmp/
RUN dpkg -i /tmp/librtlsdr0_*.deb && \
    dpkg -i /tmp/librtlsdr-dev_*.deb && \
    dpkg -i /tmp/rtl-sdr_*.deb && \
    rm -rf /tmp/*.deb && \
    echo '' | tee --append /etc/modprobe.d/rtl_sdr.conf && \
    echo 'blacklist dvb_usb_rtl28xxun' | tee --append /etc/modprobe.d/rtl_sdr.conf && \
    echo 'blacklist rtl2832' | tee --append /etc/modprobe.d/rtl_sdr.conf && \
    echo 'blacklist rtl2830' | tee --append /etc/modprobe.d/rtl_sdr.conf

# Copy rtl_airband from the build container
COPY LICENSE /opt/rtl_airband/
COPY --from=build /rtl_airband_build/build_dir/src/unittests /opt/rtl_airband/
COPY --from=build /rtl_airband_build/build_dir/src/rtl_airband /opt/rtl_airband/
RUN chmod a+x /opt/rtl_airband/unittests /opt/rtl_airband/rtl_airband

# make sure unit tests pass
RUN /opt/rtl_airband/unittests

# Use tini as init and run rtl_airband
ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/opt/rtl_airband/rtl_airband", "-F", "-e"]
