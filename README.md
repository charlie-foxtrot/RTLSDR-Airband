# RTLSDR-Airband

![main](https://github.com/charlie-foxtrot/RTLSDR-Airband/actions/workflows/build.yml/badge.svg?branch=main)

**NOTE: Going forward a release tag will be automatically created on each merge to `main`.  For the latest version, see [here](https://github.com/charlie-foxtrot/RTLSDR-Airband/releases/latest).**

**NOTE: Going forward PRs will be opened directly against `main` and the `unstable` branch will no longer be used.**

**NOTE: This repo has significantly diverged from the original project [microtony/RTLSDR-Airband](https://github.com/microtony/RTLSDR-Airband) so it has been been detached (ie no longer a fork).**

**NOTE: As of version `v5.0.0` specific build support for `rpiv1`, `armv7-generic`, and `armv8-generic` have been deprecated for the new default `native`, see [#447](https://github.com/charlie-foxtrot/RTLSDR-Airband/discussions/447)**

**NOTE: Repo URL has moved to https://github.com/charlie-foxtrot/RTLSDR-Airband see [#342](https://github.com/charlie-foxtrot/RTLSDR-Airband/discussions/342) for info**


RTLSDR-Airband receives analog radio voice channels and produces
audio streams which can be routed to various outputs, such as online
streaming services like LiveATC.net. Originally the only SDR type
supported by the program was Realtek DVB-T dongle (hence the project's
name). However, thanks to SoapySDR vendor-neutral SDR library, other
radios are now supported as well.

## Documentation

User's manual is now on the [wiki](https://github.com/charlie-foxtrot/RTLSDR-Airband/wiki).

## Credits and thanks

I hereby express my gratitude to everybody who helped with the development and testing
of RTLSDR-Airband. Special thanks go to:

* Dave Pascoe
* SDR Guru
* Marcus Ströbel
* strix-technica
* charlie-foxtrot

## License

Copyright (C) 2022-2024 charlie-foxtrot

Copyright (C) 2015-2022 Tomasz Lemiech <szpajder@gmail.com>

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

## Open Source Licenses of bundled code

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

