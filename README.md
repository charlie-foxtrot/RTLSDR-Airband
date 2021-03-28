RTLSDR-Airband
=====================

This is a development branch of RTLSDR-Airband.

**NOTE: The program is now built with cmake. Building instructions from the Wiki do not apply to this branch. Please use the following procedure:**

- Install all required dependencies as per the Wiki.
- Install cmake and pkg-config:

```
apt-get install cmake pkg-config
```

- Clone the repository and switch to the `unstable` branch:

```
git clone https://github.com/szpajder/RTLSDR-Airband.git
cd RTLSDR-Airband
git checkout unstable
```

- Build the program as follows:

```
mkdir build
cd build
cmake ../
make
make install
```

- The following options may be specified when running `cmake`:

  - `-DPLATFORM=<platform_name>` - optimize the build for the given hardware. `<platform_name>` might be one of: `rpiv1`, `rpiv2`, `armv7-generic`, `armv8-generic`, `native`.
  - `-DNFM=1` - enables narrow FM support (0 disables).
  - `-DRTLSDR=0 -DMIRISDR=0 -DSOAPYSDR=0 -DPULSEAUDIO=0` - disables respective SDR driver and/or feature. They are all enabled by default and will be built if necessary dependencies are detected.
  - `-DPROFILING=1` - enable profiling support with Google Perftools.

Example:

```
cmake -DPLATFORM=rpiv2 -DSOAPYSDR=0 -DPROFILING=1 ../
```

The Wiki will be updated once the new build system is declared stable and a new release is made.

=====================

**Current stable release: [3.2.1](https://github.com/szpajder/RTLSDR-Airband/releases/latest)** (released November 13, 2020)

RTLSDR-Airband receives analog radio voice channels and produces
audio streams which can be routed to various outputs, such as online
streaming services like LiveATC.net. Originally the only SDR type
supported by the program was Realtek DVB-T dongle (hence the project's
name). However, thanks to SoapySDR vendor-neutral SDR library, other
radios are now supported as well.

Documentation
--------------------
User's manual is now on the [wiki](https://github.com/szpajder/RTLSDR-Airband/wiki).

Credits and thanks
--------------------
I hereby express my gratitude to everybody who helped with the development and testing
of RTLSDR-Airband. Special thanks go to:

 * Dave Pascoe
 * SDR Guru
 * Marcus Ströbel
 * strix-technica
 * charlie-foxtrot

License
--------------------
Copyright (C) 2015-2021 Tomasz Lemiech <szpajder@gmail.com>

Based on original work by Wong Man Hang <microtony@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

Open Source Licenses of bundled code
---------------------
### gpu_fft
BCM2835 "GPU_FFT" release 2.0
Copyright (c) 2014, Andrew Holme.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
 * Neither the name of the copyright holder nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### rtl-sdr
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2015 by Kyle Keen <keenerd@gmail.com>
 * GNU General Public License Version 2
