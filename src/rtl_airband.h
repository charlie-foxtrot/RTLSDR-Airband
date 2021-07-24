/*
 * rtl_airband.h
 * Global declarations
 *
 * Copyright (c) 2015-2021 Tomasz Lemiech <szpajder@gmail.com>
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

#ifndef _RTL_AIRBAND_H
#define _RTL_AIRBAND_H 1
#include <cstdio>
#include <complex>
#include <stdint.h>		// uint32_t
#include <pthread.h>
#include <sys/time.h>
#include <shout/shout.h>
#include <lame/lame.h>
#include <libconfig.h++>

#include "config.h"
#ifdef WITH_BCM_VC
#include "hello_fft/gpu_fft.h"
#else
#include <fftw3.h>
#endif
#ifdef WITH_PULSEAUDIO
#include <pulse/context.h>
#include <pulse/stream.h>
#endif
#include "input-common.h"	// input_t
#include "squelch.h"

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

#define nop() do {} while (0)

#ifdef DEBUG
#define DEBUG_PATH "rtl_airband_debug.log"
#define debug_print(fmt, ...) \
	do { fprintf(debugf, "%s(): " fmt, __func__, __VA_ARGS__); fflush(debugf); } while (0)
#define debug_bulk_print(fmt, ...) \
	do { fprintf(debugf, "%s(): " fmt, __func__, __VA_ARGS__); } while (0)
#else
#define debug_print(fmt, ...) nop()
#define debug_bulk_print(fmt, ...) nop()
#endif

#define UNUSED(x) (void)(x)

#define MIN_BUF_SIZE 2560000
#define DEFAULT_SAMPLE_RATE 2560000
#ifdef NFM
#define WAVE_RATE 16000
#else
#define WAVE_RATE 8000
#endif
#define WAVE_BATCH WAVE_RATE / 8
#define AGC_EXTRA 100
#define WAVE_LEN 2 * WAVE_BATCH + AGC_EXTRA
#define MP3_RATE 8000
#define MAX_SHOUT_QUEUELEN 32768
#define TAG_QUEUE_LEN 16
#define MAX_MIXINPUTS 32

#define MIN_FFT_SIZE_LOG 8
#define DEFAULT_FFT_SIZE_LOG 9
#define MAX_FFT_SIZE_LOG 13

#define LAMEBUF_SIZE 22000 //todo: calculate
#define MIX_DIVISOR 2

#define ONES(x) ~(~0U << (x))
#define SET_BIT(a, x) (a) |= (1 << (x))
#define RESET_BIT(a, x) (a) &= ~(1 << (x))
#define IS_SET(a, x) (a) & (1 << (x))

#if defined WITH_BCM_VC
struct sample_fft_arg
{
	size_t fft_size_by4;
	GPU_FFT_COMPLEX* dest;
};
extern "C" void samplefft(sample_fft_arg *a, unsigned char* buffer, float* window, float* levels);

# define FFT_BATCH 250
#else
# define FFT_BATCH 1
#endif

//#define AFC_LOGGING

enum status {NO_SIGNAL = ' ', SIGNAL = '*', AFC_UP = '<', AFC_DOWN = '>' };
enum ch_states { CH_DIRTY, CH_WORKING, CH_READY };
enum mix_modes { MM_MONO, MM_STEREO };
enum output_type {
	O_ICECAST,
	O_FILE,
	O_RAWFILE,
	O_MIXER
#ifdef WITH_PULSEAUDIO
	, O_PULSE
#endif
};

struct icecast_data {
	const char *hostname;
	int port;
	const char *username;
	const char *password;
	const char *mountpoint;
	const char *name;
	const char *genre;
	const char *description;
	bool send_scan_freq_tags;
	shout_t *shout;
};

struct file_data {
	char *basename;
	char *suffix;
	char *file_path;
	char *file_path_tmp;
	bool continuous;
	bool append;
	bool split_on_transmission;
	timeval open_time;
	timeval last_write_time;
	FILE *f;
	enum output_type type;
};

#ifdef WITH_PULSEAUDIO
struct pulse_data {
	const char *server;
	const char *name;
	const char *sink;
	const char *stream_name;
	pa_context *context;
	pa_stream *left, *right;
	pa_channel_map lmap, rmap;
	mix_modes mode;
	bool continuous;
};
#endif

struct mixer_data {
	struct mixer_t *mixer;
	int input;
};

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

class NotchFilter
{
public:
	NotchFilter(void);
	NotchFilter(float notch_freq, float sample_freq, float q);
	void apply(float &value);

private:
	bool enabled_;
	float e;
	float p;
	float d[3];
	float x[3];
	float y[3];
};

class LowpassFilter
{
public:
	LowpassFilter(void);
	LowpassFilter(float freq, float sample_freq);
	void apply(float &r, float &j);
	bool enabled(void) const {return enabled_;}

private:
	static std::complex<double> blt(std::complex<double> pz);
	static void expand(std::complex<double> pz[], int npz, std::complex<double> coeffs[]);
	static void multin(std::complex<double> w, int npz, std::complex<double> coeffs[]);
	static std::complex<double> evaluate(std::complex<double> topco[], int nz, std::complex<double> botco[], int np, std::complex<double> z);
	static std::complex<double> eval(std::complex<double> coeffs[], int npz, std::complex<double> z);

	bool enabled_;
	float ycoeffs[3];
	float gain;

	std::complex<float> xv[3];
	std::complex<float> yv[3];
};

class Signal {
public:
	Signal(void) {
		cond_ = PTHREAD_COND_INITIALIZER;
		mutex_ = PTHREAD_MUTEX_INITIALIZER;
	}
	void send(void) {
		pthread_mutex_lock(&mutex_);
		pthread_cond_signal(&cond_);
		pthread_mutex_unlock(&mutex_);
	}
	void wait(void) {
		pthread_mutex_lock(&mutex_);
		pthread_cond_wait(&cond_, &mutex_);
		pthread_mutex_unlock(&mutex_);
	}
private:
	pthread_cond_t cond_;
	pthread_mutex_t	mutex_;
};

struct freq_t {
	int frequency;				// scan frequency
	char *label;				// frequency label
	float agcavgfast;			// average power, for AGC
	Squelch squelch;
	size_t active_counter;		// count of loops where channel has signal
	NotchFilter notch_filter;	// notch filter - good to remove CTCSS tones
	LowpassFilter lowpass_filter;	// lowpass filter, applied to I/Q after derotation, set at bandwidth/2 to remove out of band noise
	enum modulations modulation;
};
struct channel_t {
	float wavein[WAVE_LEN];		// FFT output waveform
	float waveout[WAVE_LEN];	// waveform after squelch + AGC (left/center channel mixer output)
	float waveout_r[WAVE_LEN];	// right channel mixer output
	float iq_in[2*WAVE_LEN];	// raw input samples for I/Q outputs and NFM demod
	float iq_out[2*WAVE_LEN];	// raw output samples for I/Q outputs (FIXME: allocate only if required)
#ifdef NFM
	float pr;					// previous sample - real part
	float pj;					// previous sample - imaginary part
	float alpha;
#endif
	uint32_t dm_dphi, dm_phi;	// derotation frequency and current phase value
	enum mix_modes mode;		// mono or stereo
	status axcindicate;
	unsigned char afc;			//0 - AFC disabled; 1 - minimal AFC; 2 - more aggressive AFC and so on to 255
	struct freq_t *freqlist;
	int freq_count;
	int freq_idx;
	int output_count;
	int need_mp3;
	int needs_raw_iq;
	int has_iq_outputs;
	enum ch_states state;		// mixer channel state flag
	output_t *outputs;
	int highpass;               // highpass filter cutoff
	int lowpass;                // lowpass filter cutoff
	lame_t lame;                // Context for LAME MP3 encoding if needed
	unsigned char *lamebuf;		// Buffer used by each lame encode
};

enum rec_modes { R_MULTICHANNEL, R_SCAN };
struct device_t {
	input_t *input;
#ifdef NFM
	float alpha;
#endif
	int channel_count;
	size_t *base_bins, *bins;
	channel_t *channels;
// FIXME: size_t
	int waveend;
	int waveavail;
	THREAD controller_thread;
	struct freq_tag tag_queue[TAG_QUEUE_LEN];
	int tq_head, tq_tail;
	int last_frequency;
	pthread_mutex_t tag_queue_lock;
	int row;
	int failed;
	enum rec_modes mode;
	size_t output_overrun_count;
};

struct mixinput_t {
	float *wavein;
	float ampfactor;
	float ampl, ampr;
	bool ready;
	bool has_signal;
	pthread_mutex_t mutex;
	size_t input_overrun_count;
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
	size_t output_overrun_count;
        bool is_scanner;                     // true --> this mixer acts as a scanner
        int scanner_active_input;            // index of the current input; -1 if non is active
        int hold_count;                      // counter indicating how many cycles to hold on an input; 0 means not holding but scanning
};

struct demod_params_t {
	Signal *mp3_signal;
	int device_start;
	int device_end;

#ifndef WITH_BCM_VC
	fftwf_plan fft;
	fftwf_complex* fftin;
	fftwf_complex* fftout;
#endif
};

struct output_params_t {
	Signal *mp3_signal;
	int device_start;
	int device_end;
	int mixer_start;
	int mixer_end;
};

// version.cpp
extern char const *RTL_AIRBAND_VERSION;

// output.cpp
lame_t airlame_init(mix_modes mixmode, int highpass, int lowpass);
void shout_setup(icecast_data *icecast, mix_modes mixmode);
void disable_device_outputs(device_t *dev);
void disable_channel_outputs(channel_t *channel);
void *output_check_thread(void* params);
void *output_thread(void* params);

// rtl_airband.cpp
extern bool use_localtime;
extern bool multiple_demod_threads;
extern bool multiple_output_threads;
extern char *stats_filepath;
extern size_t fft_size, fft_size_log;
extern int device_count, mixer_count;
extern int shout_metadata_delay, do_syslog, foreground;
extern volatile int do_exit, device_opened;
extern float alpha;
extern device_t *devices;
extern mixer_t *mixers;

// util.cpp
void error();
int atomic_inc(volatile int *pv);
int atomic_dec(volatile int *pv);
int atomic_get(volatile int *pv);
double atofs(char *s);
double delta_sec(const timeval *start, const timeval *stop);
void log(int priority, const char *format, ...);
void tag_queue_put(device_t *dev, int freq, struct timeval tv);
void tag_queue_get(device_t *dev, struct freq_tag *tag);
void tag_queue_advance(device_t *dev);
void sincosf_lut_init();
void sincosf_lut(uint32_t phi, float *sine, float *cosine);
void *xcalloc(size_t nmemb, size_t size, const char *file, const int line, const char *func);
void *xrealloc(void *ptr, size_t size, const char *file, const int line, const char *func);
void init_debug (char *file);
void close_debug();
extern FILE *debugf;
#define XCALLOC(nmemb, size) xcalloc((nmemb), (size), __FILE__, __LINE__, __func__)
#define XREALLOC(ptr, size) xrealloc((ptr), (size), __FILE__, __LINE__, __func__)
float dBFS_to_level(const float &dBFS);
float level_to_dBFS(const float &level);

// mixer.cpp
mixer_t *getmixerbyname(const char *name);
int mixer_connect_input(mixer_t *mixer, float ampfactor, float balance);
void mixer_disable_input(mixer_t *mixer, int input_idx);
void mixer_put_samples(mixer_t *mixer, int input_idx, float *samples, bool has_signal, unsigned int len);
void *mixer_thread(void *params);
const char *mixer_get_error();

// config.cpp
int parse_devices(libconfig::Setting &devs);
int parse_mixers(libconfig::Setting &mx);

#ifdef WITH_PULSEAUDIO
#define PULSE_STREAM_LATENCY_LIMIT 10000000UL
// pulse.cpp
void pulse_init();
int pulse_setup(pulse_data *pdata, mix_modes mixmode);
void pulse_start();
void pulse_shutdown(pulse_data *pdata);
void pulse_write_stream(pulse_data *pdata, mix_modes mode, float *data_left, float *data_right, size_t len);
#endif
#endif /* _RTL_AIRBAND_H */

// vim: ts=4
