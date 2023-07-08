FROM debian:12.0-slim AS build

# copy script files
WORKDIR /build
COPY script /build/script

# install development dependencies
RUN export DEBIAN_FRONTEND="noninteractive" \
    && apt-get update \
    && apt-get upgrade --quiet --yes \
    && apt-get install --quiet --yes --no-install-recommends \
        gettext-base       `# provides envsubst` \
        git                `# to extract commit info` \
    && script/install_debian_dependencies.sh \
    && rm -rf /var/lib/apt/lists/*

# copy the rest of the project
COPY . /build

# configure and build
RUN cmake --compile-no-warning-as-error  -B build_Release_NFM -DCMAKE_BUILD_TYPE=Release -DNFM=TRUE \
    && VERBOSE=1 cmake --build build_Release_NFM -j$(nproc)

# create .deb content structure and package it
RUN pkg_dir="$(mktemp --directory)" \
    && PACKAGE="rtl-airband" \
    VERSION="${VERSION:-$(script/snapshot_version.sh)}" \
    ARCH="$(dpkg --print-architecture)" \
    MAINTAINER="charlie-foxtrot" \
    DEPENDS="libc6 (>= 2.34), libconfig++9v5 (>= 1.5-0.4), libmp3lame0 (>= 3.100), libshout3 (>= 2.4.5), librtlsdr0 (>= 0.6.0), libsoapysdr0.8 (>= 0.8.1), libfftw3-single3 (>= 3.3.8), libpulse0 (>= 14.2)" \
    DESCRIPTION="RTLSDR Airband - A multiband decoder for AM and NFM signals" \
    script/package.sh "${pkg_dir}"

FROM debian:12.0-slim as install
COPY --from=build /build/rtl-airband*.deb /tmp
RUN export DEBIAN_FRONTEND="noninteractive" \
    && apt-get update \
    && apt-get upgrade --quiet --yes \
    && apt-get install --quiet --yes --no-install-recommends \
        /tmp/rtl-airband*.deb \
    # && rm /tmp/rtl-airband*.deb \
    && rm -rf /var/lib/apt/lists/*

# trick to get all the content without the additional "COPY" layer
#FROM scratch
#COPY --from=install / /
ENTRYPOINT ["/usr/bin/rtl_airband"]
CMD [ "-e", "-f", "-c", "/usr/share/rtl_airband/config/default.conf" ]
