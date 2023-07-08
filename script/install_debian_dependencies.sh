#! /bin/bash

set -o errexit
set -o nounset

export DEBIAN_FRONTEND="noninteractive"
apt-get update
apt-get install --quiet --yes --no-install-recommends \
    build-essential \
    cmake \
    libconfig++-dev \
    libfftw3-dev \
    libmp3lame-dev \
    libpulse-dev \
    librtlsdr-dev \
    libshout3-dev \
    libsoapysdr-dev
