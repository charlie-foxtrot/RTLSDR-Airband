RTLSDR-Airband
=====================

RTLSDR Airband is intended for AM/NFM voice channels reception and online streaming to services such as liveatc.net

Features
---------------------
 * Multichannel mode - decode up to eight AM or NFM channels per dongle (within bandwidth frequency range)
 * Scanner mode - decode unlimited number of AM and/or NFM channels with frequency hopping in a round-robin
   fashion (no frequency range limitations)
 * Decode multiple dongles simutaneously
 * Auto squelch and Automatic Gain Control
 * MP3 encoding
 * Stream to Icecast or SHOUTcast server
 * Record to local MP3 files (continuously or skipping silence periods)
 * Multiple streaming/recording destinations per channel

Performance
---------------------
### x86
 * ~4% per dongle on i5-2430m at 2.1 GHz

### Raspberry Pi
 * FFT using Broadcom Videocore IV GPU
 * No overclock required when using 1 dongle plus another dongle running dump1090
 * Turbo overclock setting recommended on RPi V1 when using 2 dongles
   (no overclocking is necessary on RPi V2)

### Other ARMv7-based platforms
 * FFT using main CPU
 * Tested on Cubieboard2 - ~50% CPU when using 1 dongle

Demo
---------------------
![Demo (Windows version)](demo.png?raw=true)

Building
---------------------
### Windows using VS Express 2013 for Desktop
#### WARNING: Windows build is currently unmaintained and might not work
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

### Linux (Raspbian)
 * Install RTLSDR library (http://sdr.osmocom.org/trac/wiki/rtl-sdr):

        sudo apt-get update
        sudo apt-get install git gcc autoconf make libusb-1.0-0-dev libtool
        cd
        git clone git://git.osmocom.org/rtl-sdr.git
        cd rtl-sdr/
        autoreconf -i
        ./configure
        make
        sudo make install
        sudo ldconfig
        sudo mv $HOME/rtl-sdr/rtl-sdr.rules /etc/udev/rules.d/rtl-sdr.rules

 * Blacklist DVB drivers to avoid conflict with SDR driver - before connecting
   the USB dongle create file `/etc/modprobe.d/dvb-blacklist.conf` with text
   editor (eg. nano) and add the following lines:

        blacklist r820t
        blacklist rtl2832
        blacklist rtl2830
        blacklist dvb_usb_rtl28xxu

 * Download a release tarball from from https://github.com/szpajder/RTLSDR-Airband/releases
   and unpack it. Example:

        tar xvfz RTLSDR-Airband-1.0.0.tar.gz
        cd RTLSDR-Airband-1.0.0

   Alternatively you can pull the latest code from git (but please be aware that it might
   be experimental, buggy, or at least, untested):

        git clone https://github.com/szpajder/RTLSDR-Airband.git
        cd RTLSDR-Airband/

 * Install necessary dependencies:

        sudo apt-get install libmp3lame-dev libvorbis-dev libshout-dev libconfig++-dev

 * Set the build options and run `make`. Available build options:

   `PLATFORM=platform_name` - selects the hardware platform. This option is mandatory. See below.

   `NFM=1` - enables NFM support. By default, NFM is disabled.

   **Warning:** Do not enable NFM, if you only use AM (especially on low-power platforms, like RPi).
   This incurs performance penalty, both for AM and NFM.

   Examples:

   Build a binary for RPi version 1 (ARMv6 CPU, Broadcom VideoCore GPU):

        PLATFORM=rpiv1 make

   Same platform, but with NFM support enabled:

        PLATFORM=rpiv1 NFM=1 make

   Building for RPi V2 (ARMv7 CPU, Broadcom VideoCore GPU), with NFM support:

        PLATFORM=rpiv2 NFM=1 make

   Building for other ARMv7-based platforms without VideoCore GPU, eg. Cubieboard (FFTW3
   library is needed in this case), NFM disabled:

        sudo apt-get install libfftw3-dev
        PLATFORM=armv7-generic make

   Building for generic x86 CPU (FFTW3 library is needed in this case), NFM enabled:

        sudo apt-get install libfftw3-dev
        PLATFORM=x86 NFM=1 make

 * Install the software:

        make install

   By default, the binary is installed to `/usr/local/bin/rtl_airband`.

 * Edit the configuration file to suit your needs. See chapter "Configuring" below.

 * You need to run the program with root privileges - for example:

        sudo /usr/local/bin/rtl_airband

   The program runs as a daemon (background process) by default, so it may look like
   it has exited right after startup. Diagnostic messages are printed to syslog
   (on Raspbian they are directed to `/var/log/messages` by default).

 * If you wish to start the program automatically at boot, you can use example startup
   scripts from `init.d` subdirectory. Example for Debian / Raspbian:

        sudo cp init.d/rtl_airband-debian.sh /etc/init.d/rtl_airband
        sudo chmod 755 /etc/init.d/rtl_airband
        sudo update-rc.d rtl_airband defaults

Configuring
--------------------
By default, the configuration is read from `/usr/local/etc/rtl_airband.conf`.
Refer to `rtl_airband.conf.example` for the description of the format. You can
copy this file to `/usr/local/etc/rtl_airband.conf` and edit it to suit your needs
(`make install` does this, unless you already have your own config file installed).

Old versions of RTLSDR-Airband used a simple flat `config.txt` file. This is no longer
supported. You have to rewrite your config file to the new syntax - or just use a
small utility `convert_cfg` which is included in the `util` subdirectory:

    cd util
    make
    ./convert_cfg /path/to/old/config.txt /path/to/new/rtl_airband.conf

Review the `rtl_airband.conf` file and move it to `/usr/local/etc` before launching
rtl_airband:

    sudo mv /path/to/new/rtl_airband.conf /usr/local/etc

Command line options
--------------------
rtl_airband accepts the following command line options:

    -h                      Display this help text
    -f                      Run in foreground, display textual waterfalls
    -c <config_file_path>   Use non-default configuration file

Troubleshooting
--------------------

Syslog logging is enabled by default, so first of all, check the logs for
any error messages, for example: `tail /var/log/messages`. Common problems:

*  Problem 1: the program fails to start on RPi. It dumps the error message:

    Dec 27 08:58:00 mypi rtl_airband[28876]: Can't open device file /dev/vcio: No such file or directory

   Solution: you need to create `/dev/vcio` device node. To do that, you need to
   know the correct major number. Older Raspbian kernels (before 4.0) used 100,
   newer ones use 249, so first check that:

    pi@mypi:~$ grep vcio /proc/devices
    249 vcio

   This means `/dev/vcio` must be created with a major number of 249:

    sudo mknod /dev/vcio c 249 0

   In other case, substitute 249 with the number taken from `grep` output above.

*  Problem 2: the program fails to start on RPi. It dumps the error message:

    Dec 27 08:58:00 mypi rtl_airband[28876]: Can't open device file /dev/vcio: No such device or address

   or:

    Dec 27 08:58:00 mypi rtl_airband[28876]: mbox_property(): ioctl_set_msg failed: Inappropriate ioctl for device

   This means you have `/dev/vcio`, but its major number is probably wrong:

    pi@mypi:~$ ls -l /dev/vcio
    crw-rw---T 1 root video 100, 0 Jan  1  1970 /dev/vcio

   This one was created with a major number of 100. `grep vcio /proc/devices`
   shows the correct number - if it's different, then delete it (`sudo rm /dev/vcio`)
   and create it again (see above).

*  Problem 3: the program fails to start on RPi. It dumps the error message:

    Unable to enable V3D. Please check your firmware is up to date.

   Most often this is a problem of people who changed the default CPU:GPU memory split
   by setting `gpu_mem` to something lower than 64 MB. Check your `/boot/config.txt`
   and remove the `gpu_mem` setting altogether, if it's there. Then save the file
   and reboot the Pi.

 * When the program is running in scan mode and is interrupted by a signal
   (eg. ctrl+C is pressed when run in foreground mode) it crashes with segmentation
   fault or spits out error messages similar to these:

    r82xx_write: i2c wr failed=-1 reg=1a len=1
    r82xx_set_freq: failed=-1
    rtlsdr_demod_write_reg failed with -1
    rtlsdr_demod_read_reg failed with -7

   This is due to a bug in libusb versions earlier than 1.0.17. Upgrade to a newer
   version.

License
--------------------
Copyright (C) 2015-2016 Tomasz Lemiech <szpajder@gmail.com>

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

Open Source Licenses
---------------------

###fftw3
 * Copyright (c) 2003, 2007-14 Matteo Frigo
 * Copyright (c) 2003, 2007-14 Massachusetts Institute of Technology
 * GNU General Public License Version 2

###gpu_fft
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

###libconfig
Copyright (C) 2005-2014  Mark A Lindner

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public License
published by the Free Software Foundation; either version 2.1 of
the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.
