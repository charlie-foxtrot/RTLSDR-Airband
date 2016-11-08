/*
 * rtl_airband.h
 * Global declarations
 *
 * Copyright (c) 2015-2016 Tomasz Lemiech <szpajder@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <pthread.h>
#include <sys/time.h>
#include <shout/shout.h>
#include <lame/lame.h>
#include <libconfig.h++>
#include <rtl-sdr.h>
#ifdef USE_BCM_VC
#include "hello_fft/gpu_fft.h"
#endif

#define RTL_AIRBAND_VERSION "2.2.0rc1"
#define ALIGN
#define ALIGN2 __attribute__((aligned(32)))
#define SLEEP(x) usleep(x * 1000)
#define THREAD pthread_t
#define GOTOXY(x, y) printf("%c[%d;%df",0x1B,y,x)
#ifndef SYSCONFDIR
#define SYSCONFDIR "/usr/local/etc"
#endif
#define CFGFILE SYSCONFDIR "/rtl_airband.conf"
#define PIDFILE "/run/rtl_airband.pid"
#if DEBUG
#define DEBUG_PATH "rtl_airband_debug.log"
#endif

#define BUF_SIZE 2560000
#define SOURCE_RATE 2560000
#ifdef NFM
#define WAVE_RATE 16000
#else
#define WAVE_RATE 8000
#endif
#define WAVE_BATCH WAVE_RATE / 8
#define AGC_EXTRA WAVE_RATE / 160
#define WAVE_LEN 2 * WAVE_BATCH + AGC_EXTRA
#define MP3_RATE 8000
#define MAX_SHOUT_QUEUELEN 32768
#define TAG_QUEUE_LEN 16
#define CHANNELS 8
#define MAX_MIXINPUTS 32
#define FFT_SIZE_LOG 9
#define LAMEBUF_SIZE 22000 //todo: calculate
#define MIX_DIVISOR 2

#define ONES(x) ~(~0 << (x))
#define SET_BIT(a, x) (a) |= (1 << (x))
#define RESET_BIT(a, x) (a) &= ~(1 << (x))
#define IS_SET(a, x) (a) & (1 << (x))

#if defined USE_BCM_VC
struct sample_fft_arg
{
	int fft_size_by4;
	GPU_FFT_COMPLEX* dest;
};
extern "C" void samplefft(sample_fft_arg *a, unsigned char* buffer, float* window, float* levels);

# define FFT_BATCH 250
#else
# define FFT_BATCH 1
#endif
#define FFT_SIZE (2<<(FFT_SIZE_LOG - 1))

//#define AFC_LOGGING

struct icecast_data {
	const char *hostname;
	int port;
	const char *username;
	const char *password;
	const char *mountpoint;
	const char *name;
	const char *genre;
	bool send_scan_freq_tags;
	shout_t *shout;
};

struct file_data {
	const char *dir;
	const char *prefix;
	char *suffix;
	bool continuous;
	bool append;
	FILE *f;
};

struct mixer_data {
	struct mixer_t *mixer;
	int input;
};

enum output_type { O_ICECAST, O_FILE, O_MIXER };
struct output_t {
	enum output_type type;
	bool enabled;
	bool active;
	void *data;
};

struct freq_tag {
	int freq;
	struct timeval tv;
};

enum modulations {
	MOD_AM
#ifdef NFM
	, MOD_NFM
#endif
};

enum ch_states { CH_DIRTY, CH_WORKING, CH_READY };
struct channel_t {
	float wavein[WAVE_LEN];		// FFT output waveform
	float waveref[WAVE_LEN];	// for power level calculation
	float waveout[WAVE_LEN];	// waveform after squelch + AGC
#ifdef NFM
	float complex_samples[2*WAVE_LEN];	// raw samples for NFM demod
	float timeref_nsin[WAVE_RATE];
	float timeref_cos[WAVE_RATE];
	int wavecnt;				// sample counter for timeref shift
// FIXME: get this from complex_samples?
	float pr;					// previous sample - real part
	float pj;					// previous sample - imaginary part
	float alpha;
#endif
	enum modulations modulation;
	int agcsq;					// squelch status, 0 = signal, 1 = suppressed
	float agcavgfast;			// average power, for AGC
	float agcavgslow;			// average power, for squelch level detection
	float agcmin;				// noise level
	int sqlevel;				// manually configured squelch level
	int agclow;					// low level sample count
	char axcindicate;			// squelch/AFC status indicator: ' ' - no signal; '*' - has signal; '>', '<' - signal tuned by AFC
	unsigned char afc;			//0 - AFC disabled; 1 - minimal AFC; 2 - more aggressive AFC and so on to 255
	int frequency;
	int freq_count;
	int *freqlist;
	char **labels;
	int output_count;
	int need_mp3;
	enum ch_states state;		// mixer channel state flag
	output_t *outputs;
	lame_t lame;
};

enum rec_modes { R_MULTICHANNEL, R_SCAN };
struct device_t {
	unsigned char buffer[BUF_SIZE + FFT_SIZE * 2 + 48];
	int bufs;
	int bufe;
	rtlsdr_dev_t* rtlsdr;
	int device;
	int centerfreq;
	int correction;
	int gain;
#ifdef NFM
	float alpha;
#endif
	int channel_count;
	int base_bins[8];
	int bins[8];
	channel_t channels[8];
	int waveend;
	int waveavail;
	THREAD rtl_thread;
	THREAD controller_thread;
	struct freq_tag tag_queue[TAG_QUEUE_LEN];
	int tq_head, tq_tail;
	int last_frequency;
	pthread_mutex_t tag_queue_lock;
	pthread_mutex_t buffer_lock;
	int row;
	int failed;
	enum rec_modes mode;
};

struct mixinput_t {
	float *wavein;
	float ampfactor;
	bool ready;
	pthread_mutex_t mutex;
};

struct mixer_t {
	const char *name;
	bool enabled;
	int input_count;
	int interval;
	unsigned int inputs_todo;
	unsigned int input_mask;
	channel_t channel;
	mixinput_t inputs[MAX_MIXINPUTS];
};

// output.cpp
lame_t airlame_init();
void shout_setup(icecast_data *icecast);
void disable_device_outputs(device_t *dev);
void disable_channel_outputs(channel_t *channel);
void *icecast_check(void* params);
void *output_thread(void* params);

// rtl_airband.cpp
extern bool use_localtime;
extern int device_count, mixer_count;
extern int shout_metadata_delay, do_syslog, foreground;
extern volatile int do_exit;
extern float alpha;
extern device_t *devices;
extern mixer_t *mixers;
extern pthread_cond_t mp3_cond;
extern pthread_mutex_t mp3_mutex;

// util.cpp
void error();
int atomic_inc(volatile int *pv);
int atomic_dec(volatile int *pv);
int atomic_get(volatile int *pv);
void log(int priority, const char *format, ...);
void tag_queue_put(device_t *dev, int freq, struct timeval tv);
void tag_queue_get(device_t *dev, struct freq_tag *tag);
void tag_queue_advance(device_t *dev);
void init_debug (char *file);
void close_debug();
extern FILE *debugf;
#define debug_print(fmt, ...) \
	do { if (DEBUG) fprintf(debugf, "%s(): " fmt, __func__, __VA_ARGS__); fflush(debugf); } while (0)
#define debug_bulk_print(fmt, ...) \
	do { if (DEBUG) fprintf(debugf, "%s(): " fmt, __func__, __VA_ARGS__); } while (0)

// mixer.cpp
mixer_t *getmixerbyname(const char *name);
int mixer_connect_input(mixer_t *mixer, float ampfactor);
void mixer_disable_input(mixer_t *mixer, int input_idx);
void mixer_put_samples(mixer_t *mixer, int input_idx, float *samples, unsigned int len);
void *mixer_thread(void *params);
const char *mixer_get_error();

// config.cpp
int parse_devices(libconfig::Setting &devs);
int parse_mixers(libconfig::Setting &mx);

// vim: ts=4
