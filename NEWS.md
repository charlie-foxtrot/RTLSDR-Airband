NEWS:

Version 3.1.0 (Jan 19, 2020)
 * SoapySDR: added support for complex float 32-bit samples
 * SoapySDR: allow using AGC if the device supports it. Gain setting for
   soapy devices is now optional - if it's not specified, the program will
   try to enable AGC.
 * Use lowpass/highpass filters provided by LAME library to improve audio
   quality of MP3 streams. Filter cutoff frequencies may be configured per
   output, using `highpass` and `lowpass` config options. Credit: clydebarrow.
 * Added `log_scan_activity` global config option. When set to `true`, a
   log message is written whenever a squelch opens on a scanned channel,
   effectively producing a channel activity log. Credit: clam-i-am.
 * Improved squelch behaviour in some corner cases.
 * Fix for incorrect naming of pulseaudio context. Name set in the config
   was not used as it should. Credit: Darryl Pogue.
 * Don't fail when the configured gain value is negative. Some SDRs support
   this (eg. FC0012-based dongles).
 * Fix a bug which in some cases could prevent the icecast output from
   reconnecting with the Icecast server after the connection has failed.

Version 3.0.1 (Feb 16, 2018)
 * Fix for squelch staying constantly open when configured manually
   with NFM=off (#84)

Version 3.0.0 (Feb 10, 2018):
 * Major overhaul of the SDR input code - now it's modular and
   hardware-agnostic (no longer tightly coupled with librtlsdr).
 * Support for SoapySDR vendor-neutral SDR library - any SDR which has
   a plugin for SoapySDR shall now work in RTLSDR-Airband.
 * Support for Mirics DVB-T dongles via libmirisdr-4 library.
 * Support for RTLSDR is now optional and can be disabled at compilation
   stage.
 * Removed the 8-channels-per-device limit in multichannel mode.
 * Configurable per-device sampling rate.
 * Configurable FFT size.
 * Support for multibyte input samples.
 * Support for rawfile outputs (ie. writing raw I/Q data from a
   narrowband channel to a file for processing with other programs,
   line GNUradio or csdr).
 * INCOMPATIBLE CHANGE: removed `rtlsdr_buffers` global configuration
   option; buffer count can now be adjusted with a per-device
   "buffers" option.
 * INCOMPATIBLE CHANGE: removed `syslog` global configuration option;
   syslog logging is now enabled by default, both in foreground and
   background mode. To force logging to standard error, use -e command
   line option.
 * Added -F command line option for better cooperation with systemd.
   Runs the program in foreground, but without textual waterfalls.
   Together with -e it allows running rtl_airband as a service of type
   "simple" under systemd. Example rtl_airband.service file has been
   adjusted to reflect this change.
 * Added `type` device configuration option. It sets the device type
   (ie. the input driver which shall be used to talk to the device).
   "rtlsdr" is assumed as a default type for backward compatibility.
   If RTLSDR support has been disabled at compilation stage, then
   there is no default type - it must be set manually, or the program
   will throw an error on startup.
 * Frequencies in the config can now be expressed in Hz, kHz, MHz or GHz
   for improved readability.
 * Lots of bugfixes.
 * Rewritten documentation on [Github Wiki](https://github.com/szpajder/RTLSDR-Airband/wiki).

Version 2.4.0 (Oct 15, 2017):
 * Support for PulseAudio output via new output type `pulse`. With this
   feature you can eg. play the sound via the soundcard of the Raspberry
   Pi you run RTLSDR-Airband on (you need to install and run pulseaudio
   daemon on it, though). Or you can stream the audio from a Pi located
   near the antenna (eg. in the attic) to speakers connected to the desktop
   PC you are sitting at, without launching a local Icecast server,
   as before. Because the audio stream is sent uncompressed, it is
   not recommended to run it across the Internet - jitter or packet loss
   will easily cause the audio to become choppy. However in a local network
   PulseAudio is a good choice. And it gives much lower latency as compared
   to Icecast (typically under 0.5 seconds). Thanks to Marcus Ströbel
   for the idea and initial implementation.
 * Support for referring to RTL devices by their serial numbers in the
   config file. Instead of `index = <dongle_index>` parameter, use `serial =
   <dongle_serial_number>` to get consistent behavior across reboots
   and hardware reconfigurations.
 * Set RTL gain to the nearest gain value supported by the device. This is
   required for E4000 tuners, which do not round the given gain value to
   the nearest supported setting, which causes the gain setting operation
   to fail.
 * Improved squelch operation in scan mode. All squelch-related variables
   (noise floor, AGC coefficients, etc) are now calculated and stored
   separately for each scanned channel. Earlier their values were common
   to all channels, which caused squelch problems in case when noise floor
   varied considerably between channels. Thanks to @strix-technica.
 * Added build target for FreeBSD on x86. Use `PLATFORM=x86-freebsd` to
   compile and `PLATFORM=x86-freebsd gmake install` to install. Thanks
   to @nyammy.
 * Display squelch setting in waterfall in place of noise floor value when
   squelch is set manually.
 * Bug fixes, performance improvements.
 * Decluttered and more understandable documentation.

Version 2.3.0 (Jan 2, 2017):
 * Added support for mixers. It is now possible to produce audio streams
   combined from several input channels. Both mono and stereo mixing is
   supported. Usage example is provided in config/mixers.conf. All
   mixer-related parameters are documented in config/reference.conf.
 * Added build options for 64-bit ARM architectures, like Odroid C2.
   Please use PLATFORM=armv8-generic when compiling.
 * Fixed a long-standing bug in RTL sample processing, which caused some
   samples to be processed twice. If you were annoyed by these regular
   clicks in NFM audio every 125 ms, they are now gone.
 * Reduced CPU usage on x86
 * Some code restructuring and cleanups
 * Added several configuration file examples for typical real-life
   scenarios. They are placed in config/ subdirectory. rtl_airband.conf.example
   file has been moved to config/reference.conf. It is meant to be a reference
   for all supported config knobs together with their description. This is
   still an interim solution before some more readable and understandable
   documentation gets written.

Version 2.2.0 (Oct 8, 2016):
 * Support for Icecast stream metadata updates in scanning mode. When enabled,
   every time the scanner stops on a channel, current frequency is written into
   Icecast song title, which in turn is displayed in the player. Alternatively,
   textual labels can be configured for each frequency. It is possible
   to configure the amount of delay between the stream and metadata updates to
   synchronize them with the audio. There are some caveats however - read
   comments in rtl_airband.conf.example for details.
 * Added global option 'localtime'. When enabled, rtl_airband uses local time
   instead of UTC time for output file names. (Credit: ScanOC).
 * Auto gain feature removed. RTL auto gain does not work well for narrowband
   channels. Most often it sets the gain too high which causes problems for
   auto squelch and audio bleeding between adjacent channels. Gain must be
   configured manually from now on.
 * Dropped unmaintained Windows build.
 * Reverted to power level calculation algorithm from version 2.0.2. The new
   algo didn't really do much to sensitivity, but introduced annoying clicks
   on squelch open/close.
 * Improved DC offset estimator for AM mode. This one hardly ever clicks
   on squelch opening.
 * Boosted AM audio volume.
 * Reduced squelch flapping in NFM mode.

Version 2.1.0 (Aug 11, 2016):
 * Narrowband FM demodulation support
 * Automatic Frequency Control
 * Append mode for recording (enabled by default)
 * Dongles, channels and outputs can be individually enabled and disabled
   by a simple config flag (no need to comment out or delete large
   configuration sections)
 * Use VBR for MP3 encoding
 * Modified power level calculation algorithm (better sensitivity)
 * Support for manual squelch setting
 * Bug fixes

Version 2.0.2 (Mar 26, 2016):
 * Fixed a problem with running three dongles or more, simultaneously

Version 2.0.1 (Jan 24, 2016):
 * Fixed crash on output initialization

Version 2.0.0 (Dec 27, 2015):
 * New libconfig-style config file format
 * util/convert_cfg: can be used to convert old-style config.txt to the new format
 * Syslog logging (enabled by default)
 * Daemon mode
 * Reworked makefiles, added install rule
 * /dev/vcio is now used to access GPU on RPi; creating char_dev no longer necessary
 * Startup scripts for Debian and Gentoo
 * Support for auto gain setting
 * Support for multiple outputs per channel
 * Support for recording streams to local MP3 files
 * Support for ARMv7-based platforms other than RPi (eg. Cubieboard)
 * Updated documentation
 * Numerous bugfixes and stability improvements

Version 1.0.0 (May 12, 2015):
 * Linux x86/x86_64 support (Windows build is currently unmaintained and might not work)
 * Raspberry Pi V2 support
 * Bundled hello_fft code (v2.0)
 * More robust interaction with Icecast servers
 * Important stability fixes
