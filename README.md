RTLSDR-Airband
=====================

RTLSDR-Airband is a program intended for AM/NFM voice channels reception and online streaming
to services such as LiveATC.net.

Features
---------------------
 * Multichannel mode - decode up to eight AM or NFM channels per dongle (within bandwidth frequency range)
 * Scanner mode - decode unlimited number of AM or NFM channels with frequency hopping in a round-robin
   fashion (no frequency range limitations)
 * Decode multiple dongles simultaneously
 * Auto squelch
 * Automatic volume equalization for AM
 * MP3 encoding
 * Stream to Icecast or SHOUTcast server
 * Record to local MP3 files (continuously or skipping silence periods)
 * Stream uncompressed audio to a PulseAudio server (eg. for local or networked playback)
 * Multiple streaming/recording destinations per channel
 * Mixing several channels into a single audio stream (both mono and stereo mixing is supported)

Supported and tested platforms
---------------------
 * GNU/Linux
   * x86/x86_64
   * ARMv6 (Raspberry Pi v1)
   * ARMv7 (Raspberry Pi v2, v3, Cubieboard)
   * ARMv8
 * FreeBSD
   * x86/x86_64

Screenshot
---------------------
![Screenshot](demo.png?raw=true)

Building
---------------------
The following instructions are specific to Raspbian Linux, because it is the most
prevalent distribution for Raspberry Pi. However the software should build and
run on pretty much any reasonably modern Linux distribution.

### Dependencies

The following third-party software is required to compile RTLSDR-Airband. Raspian
package names are given in parentheses:

 * GCC compiler (gcc)
 * GNU make (make)
 * LAME mp3 encoder (libmp3lame-dev)
 * libshout library (libshout3-dev)
 * libconfig library (libconfig++-dev)
 * rtl-sdr library (librtlsdr-dev)

Additionally, on a Raspberry Pi:

 * Broadcom VideoCore GPU development headers and libraries (libraspberrypi-dev)

and for platforms other than Raspberry Pi:

 * FFTW library (libfftw3-dev)

This software is packaged in most Linux distributions. In Raspbian you can install
it all at once with a single command:

        sudo apt-get install gcc make libmp3lame-dev libshout3-dev libconfig++-dev librtlsdr-dev libraspberrypi-dev

On platforms other than Raspberry Pi you also need this:

        sudo apt-get install libfftw3-dev

Then you have to blacklist DVB drivers for RTL chipset to avoid conflict with SDR
applications. Using any text editor (eg. `nano`) create a file named
`/etc/modprobe.d/dvb-blacklist.conf` and put the following lines in it:

        blacklist r820t
        blacklist rtl2832
        blacklist rtl2830
        blacklist dvb_usb_rtl28xxu

Save the file and exit the editor.

### Compilation

You may either go for a stable release (these are quite well tested) or the latest
development code from Git repository (which has all the cutting-edge features and
bugs as well). New code always goes to the `unstable` branch first before it is
merged into `master` branch and a release is made.

To download a release tarball, go to https://github.com/szpajder/RTLSDR-Airband/releases ,
get the latest one and unpack it. Example:

        tar xvfz RTLSDR-Airband-2.3.0.tar.gz
        cd RTLSDR-Airband-2.3.0

If you prefer to run the latest development code, then do this:

        git clone https://github.com/szpajder/RTLSDR-Airband.git
        cd RTLSDR-Airband/
        git checkout unstable

The build process is controlled with environment variables passed to the `make` program.
Just type:

        make

to list all available build options. Currently the only mandatory option is PLATFORM,
which is used to set the hardware platform you are building for. Setting this option
enables hardware-specific code optimizations. For example, on a Raspberry Pi the
Fast Fourier Transform calculation is offloaded to the Broadcom VideoCore GPU, which
is crucial for good performance. Of course you can build the program without GPU
support, but it will cause main CPU utilization to be prohibitively high. On the other
hand, it does not make sense to include VC GPU support on a platform which is not
equipped with it (eg. desktop x86 PC).

Build options are specified before (or after) `make` command. For example, to compile
the program for Raspberry Pi version 1, type:

        PLATFORM=rpiv1 make

This enables VC GPU support together with some code optimized for ARMv6 CPU.

Other supported PLATFORM values:

   * `rpiv2` - for Raspberry Pi v2 and v3 (ARMv7 CPU, Broadcom VideoCore GPU)
   * `armv7-generic` - for ARMv7-based platforms which do not have VC GPU, eg. Cubieboard
   * `armv8-generic` - for 64-bit ARM platforms, eg. Odroid C2
   * `x86` - for your desktop PC running Linux
   * `x86-freebsd` - for your desktop PC running FreeBSD (probably you need to use `gmake` instead of `make`)

### Enabling optional features

##### Narrowband FM demodulation

By default only AM demodulation is enabled. This is enough for VHF airband. If you want
to use the program to monitor other things, like rail frequencies or public services,
you need NFM support.

If you have compiled the program before, type:

        make clean

to start from a fresh build. **Note: do this every time you decide to change your
set of build options.** Then type:

        PLATFORM=<your_platform> NFM=1 make

**Warning:** Do not enable NFM, if you do not plan to use it (especially on low-performance
platforms, like RPi). Enabling NFM incurs performance penalty, both for AM and NFM.

##### PulseAudio support

PulseAudio is a sound server which allows sharing audio equipment (read: sound card(s))
across many sound sources (applications). It is a default sound server in pretty much
every mainstream Linux distribution.

RTLSDR-Airband can stream uncompressed audio to a PulseAudio server for real-time playback.
The server may run on the same machine or on another one, reachable over your local network.
Using PulseAudio you can, for example, play the sound via the soundcard of the Raspberry Pi
you run RTLSDR-Airband on. Or you can stream the audio from a Pi located near the antenna
(eg. in the attic) to speakers connected to the desktop PC you are sitting at. Of course
you can do the same thing with Icecast, but with PulseAudio it comes with less hassle -
you don't need to set up a local Icecast server, you don't need to launch VLC or other
player, you get a per-stream volume knob in your mixer. Audio quality is better (no MP3
compression) and latency is really low (usually below half a second).

First, install PulseAudio library and headers:

        apt-get install libpulse-dev

Recompile RTLSDR-Airband with PULSE=1 build option:

        make clean
        PLATFORM=<your_platform> PULSE=1 make

### Installation

After RTLSDR-Airband is built, install it:

        make install

On FreeBSD it's a little different:

        PLATFORM=x86-freebsd gmake install

The binary will be installed to `/usr/local/bin/rtl_airband`.

Configuring
--------------------
By default, the configuration is read from `/usr/local/etc/rtl_airband.conf`.
Refer to example configuration files in the config/ subdirectory. basic_multichannel.conf
is a good starting point. When you do `make install`, this file will get copied to
`/usr/local/etc/rtl_airband.conf` (unless you already have your own config file installed).
All available config parameters are mentioned and documented in config/reference.conf.

Running the program
--------------------
rtl_airband accepts the following command line options:

    -h                      Display this help text and exit
    -v                      Display version and exit
    -f                      Run in foreground, display textual waterfalls
    -c <config_file_path>   Use non-default configuration file
    -Q                      Use quadri correlator for FM demodulation (default is atan2)
                            (this option is available only if rtl_airband was compiled
                            with NFM support enabled)

On Raspberry Pi you need to run the program with root privileges, otherwise it won't be
able to access GPU hardware:

        sudo /usr/local/bin/rtl_airband

On other arches you can run the program with user privileges. The only prerequisite is
to have access rights to USB devices. Check `/dev/bus/usb` directory for permissions of
device nodes - most probably they are writable by a group `usb` or `plugdev` or similar.
Just add yourself to that group and rtl_airband should run fine.

The program runs as a daemon (background process) by default, so it may look like it has
exited right after startup. Diagnostic messages are printed to syslog (on Raspbian Jessie
they are directed to `/var/log/daemon.log` by default).

If you wish to start the program automatically at boot, you can use example startup
scripts from `init.d` subdirectory. Example for Debian / Raspbian Jessie or newer (or
any other distribution based on systemd):

        sudo cp init.d/rtl_airband.service /etc/systemd/system
        sudo systemctl daemon-reload
        sudo systemctl enable rtl_airband

Example for Debian / Raspbian Wheezy or older (sysvinit-based):

        sudo cp init.d/rtl_airband-debian.sh /etc/init.d/rtl_airband
        sudo chmod 755 /etc/init.d/rtl_airband
        sudo update-rc.d rtl_airband defaults

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

Credits and thanks
--------------------
I hereby express my gratitude to everybody who helped with the development and testing
of RTLSDR-Airband. Special thanks go to:

 * Dave Pascoe
 * SDR Guru
 * Marcus Ströbel
 * strix-technica

License
--------------------
Copyright (C) 2015-2017 Tomasz Lemiech <szpajder@gmail.com>

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
