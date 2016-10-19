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

#include <pthread.h>
#include <rtl-sdr.h>

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
#define FFT_SIZE_LOG 9
#define LAMEBUF_SIZE 22000 //todo: calculate

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

enum output_type { O_ICECAST, O_FILE };
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
	int row;
	int failed;
	enum rec_modes mode;
};

// output.cpp
lame_t airlame_init();
void shout_setup(icecast_data *icecast);
void *icecast_check(void* params);
void *output_thread(void* params);

// rtl_airband.cpp
extern bool use_localtime;
extern int device_count;
extern int shout_metadata_delay;
extern device_t *devices;
extern volatile int do_exit;
extern pthread_cond_t mp3_cond;
extern pthread_mutex_t mp3_mutex;
void tag_queue_get(device_t *dev, struct freq_tag *tag);
void tag_queue_advance(device_t *dev);


void log(int priority, const char *format, ...);

