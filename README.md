RTLSDR-Airband
=====================

RTLSDR Airband is intended for Airband reception and online streaming to services such as liveatc.net

Features
---------------------
 * Decode multiple AM channels per dongle (within bandwidth frequency range)
 * Auto squelch and Automatic Gain Control
 * MP3 encoding
 * Stream to Icecast or SHOUTcast server
 * Low CPU usage on Windows (<4% on i5-2430m) thanks to SSE and AVX instructions
 * FFT using GPU on Raspberry Pi (1.28Msamples/s, 50-55% CPU with default clock)

Demo
---------------------
![Demo (Windows version)](demo.png?raw=true)

Windows Binary
---------------------
The latest Windows binary can be downloaded [here](http://www.microtony.com/rtl_airband.zip).

Building
---------------------
### Windows using VS Express 2013 for Desktop
#### Getting/Building prerequisites
##### FFTW3
 * Download fftw-3.3.4-dll32.zip from http://www.fftw.org/install/windows.html
 * Start the VS2013 x86 Native Tools Command Prompt
 * Run lib /def:libfftw3f-3.def to get libfftw3f-3.lib
 * Place the lib into win32/lib
 * Place fftw3.h into win32/include
 * Place libfftw3f-3.dll into the root folder
 
##### libogg and libvorbis
 * Download libogg-1.3.1.zip and libvorbis-1.3.4.zip from http://www.xiph.org/downloads/
 * Extract both zip files in the same folder
 * Rename libogg-1.x.x to libogg
 * Open libogg/win32/libogg_static.sln
 * Change to "Release" and build libogg_static
 * Open libvorbis-1.x.x/win32/VS2010/vorbis_static.sln
 * Change to "Release" and build libvorbis_static **(NOT the highlighted libvorbisfile)**
 * Copy both the generated .libs to win32/lib
 * Copy libogg/includes/ogg (the folder) to win32/include
 * Copy libvorbis-1.x.x/includes/vorbis (the folder) to win32/include
 
##### libmp3lame
 * Download from http://sourceforge.net/projects/lame/files/lame/3.99/
 * Open vc_solution/vc9_lame.sln
 * Change the Configuration to ReleaseSSE2
 * (optional) Right click libmp3lame-static and open Properties... C/C++... Code Generation... Enable Enhanced Instruction Set... Advanced Vector Instructions
 * Build libmp3lame-static
 * Copy output/ReleaseSSE2/libmp3lame-static.lib and libmpghip-static.lib to win32/lib
 * Copy include/lame.h to win32/include
 
##### librtlsdr
 * Download from http://sdr.osmocom.org/trac/wiki/rtl-sdr (search word: "pre-built")
 * Copy rtl-sdr-release/rtl-sdr.h and rtl-sdr_export.h to win32/includes
 * Copy rtl-sdr-release/x32/rtlsdr.lib to win32/lib
 * Copy rtl-sdr-release/x32/rtlsdr.dll and libusb-1.0.dll to root folder
 
##### pthreads
 * Download from ftp://sourceware.org/pub/pthreads-win32/
 * Extract Pre-built.2 to the same folder where libogg and libvorbis are located
 * Copy Pre-built.2/lib/x86/pthreadVCE2.lib to win32/lib
 * Copy Pre-built.2/dll/x86/pthreadVCE2.dll to root folder

##### libshout
 * This is the most complicated one
 * Download from http://www.icecast.org/download.php
 * Extract libshout to the same folder where libogg and libvorbis are located
 * Go to libshout-2.x.x/include/shout and rename shout.h.in to shout.h
 * Open libshout/win32/libshout.dsw
 * Change to "Release"... and then open project properties
 * On the General page change Character Set to Use Unicode Character Set
 * Go to C/C++... edit Additional Include Directories
 * Add ../../libogg/include, ../../libvorbis-1.x.x/include and ../../Pre-built.2/include
 * Go to C/C++... Preprocessor... edit Preprocessor Definitions
 * Add HAVE_SYS_TIMEB_H and HAVE_FTIME
 * Try to build, you should get errors
 * One of the error is "error C1083: Cannot open include file: 'compat.h': No such file or directory"
 * Double click on it and change that line from #include &lt;compat.h&gt; to #include &lt;os.h&gt;
 * Build again
 * Copy win32/Release/libshout.lib to win32/lib
 * Copy include/shout (the folder) to win32/include
 
#### Building this project
 * Browse into win32 and open the solution
 * Simply build solution or click the "Start Local Windows Debugger" button
 
### Raspberry Pi (Raspbian)
 * I assume you already have RTLSDR library installed: http://www.hamradioscience.com/raspberry-pi-as-remote-server-for-rtl2832u-sdr/
 * sudo apt-get install libmp3lame-dev libvorbis-dev libshout-dev 
 * sudo rpi-update && sudo reboot
 * cd into the project folder (where makefile is located)
 * cp -r /opt/vc/src/hello_pi/hello_fft .
 * sudo mknod char_dev c 100 0
 * make
 * You need to run the program with sudo

Configuring
--------------------
All configurations are saved in the config folder

Naming: configname.txt

Format:

    Server hostname
    Server port
    Username (default: source)
    Password
    Center frequency in Hz
    Frequency correction (negative = signal appears to have lower frequency)
    Gain x 10 (e.g. 0, ... 280, 297, 338, ... 496)
    Frequency [Mountpoint]
    ... Repeat 7 more times ...
    ... Frequency = 0 to disable ...

Example:

    icecast.microtony.com
    80
    source
    ABCDEFGH
    120118000
    -8000
    297
    118925000 118925
    119100000 119100
    119500000 119500
    120600000 120600
    121000000 121000
    121300000 121300
    0
    0

Usage
--------------------
There are two ways to start the program

### Command line argument

    rtl_airband <device> <configname>
    Example: rtl_airband 0 approach
    
    
### Interactive

Program will prompt you for the RTLSDR device ID and config name

License
--------------------
Copyright (C) 2014 Wong Man Hang <microtony@gmail.com>

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
    
Open Source Licenses
---------------------

###fftw3
 * Copyright (c) 2003, 2007-14 Matteo Frigo
 * Copyright (c) 2003, 2007-14 Massachusetts Institute of Technology
 * GNU General Public License Version 2

###gpu_fft
Copyright (c) 2013, Andrew Holme.
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
    
###libmp3lame
 *      Copyright (c) 1999 Mark Taylor
 *      Copyright (c) 2000-2002 Takehiro Tominaga
 *      Copyright (c) 2000-2011 Robert Hegemann
 *      Copyright (c) 2001 Gabriel Bouvigne
 *      Copyright (c) 2001 John Dahlstrom
 *  GNU Library General Public License Version 2

###libogg
Copyright (c) 2002, Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

###librtlsdr
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * GNU General Public License Version 2

###libshout
 * Copyright (C) 2002-2004 the Icecast team <team@icecast.org>
 * GNU Library General Public License Version 2

###libvorbis
Copyright (c) 2002-2008 Xiph.org Foundation

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

- Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

- Neither the name of the Xiph.org Foundation nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
