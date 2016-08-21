/*
 * RTLSDR AM demodulator and streaming
 * 
 * Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
 * Copyright (c) 2015-2016 Tomasz Lemiech <szpajder@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define RTL_AIRBAND_VERSION "2.1.0"
#if defined USE_BCM_VC && !defined __arm__
#error Broadcom VideoCore support can only be enabled on ARM builds
#endif

// From this point we may safely assume that USE_BCM_VC implies __arm__

#if defined _WIN32

#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES
#include <SDKDDKVer.h>
#include <windows.h>
#include <process.h>
#include <complex>
#include <MMSystem.h>
#include <xmmintrin.h>
#define ALIGN __declspec(align(32))
#define ALIGN2
#define SLEEP(x) Sleep(x)
#define THREAD HANDLE
#define GOTOXY(x, y) { COORD xy; xy.X = x; xy.Y = y; SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), xy); }
#define scanf scanf_s
#define sscanf sscanf_s
#define fscanf fscanf_s
#define CFGFILE "rtl_airband.conf"

#elif defined __arm__

#ifdef USE_BCM_VC
#include "hello_fft/mailbox.h"
#include "hello_fft/gpu_fft.h"
#else
#include <fftw3.h>
#endif

#else   /* x86 */
#include <xmmintrin.h>
#include <fftw3.h>

#endif /* x86 */

#ifndef _WIN32
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
#define AUTO_GAIN -100
#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <algorithm>
#include <csignal>
#include <cstdarg>
#include <cerrno>
#endif /* !_WIN32 */ 

#include <iostream>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <libconfig.h++>

#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>
#include <shout/shout.h>
#ifdef _WIN32
#include <lame.h>
#else
#include <lame/lame.h>
#endif

#include <rtl-sdr.h>

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

#if defined _WIN32
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#endif /* _WIN32 */

using namespace std;
using namespace libconfig;

enum output_type { O_ICECAST, O_FILE };
struct output_t {
    enum output_type type;
    bool enabled;
    bool active;
    void *data;
};

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
    float wavein[WAVE_LEN];  // FFT output waveform
    float waveout[WAVE_LEN]; // waveform after squelch + AGC
#ifdef NFM
    float complex_samples[2*WAVE_LEN];  // raw samples for NFM demod
    float timeref_nsin[WAVE_RATE];
    float timeref_cos[WAVE_RATE];
    int wavecnt;             // sample counter for timeref shift
// FIXME: get this from complex_samples?
    float pr;                // previous sample - real part
    float pj;                // previous sample - imaginary part
    float alpha;
#endif
    enum modulations modulation;
    int agcsq;             // squelch status, 0 = signal, 1 = suppressed
    float agcavgfast;  // average power, for AGC
    float agcavgslow;  // average power, for squelch level detection
    float agcmin;      // noise level
    int sqlevel;     // manually configured squelch level
    int agclow;             // low level sample count
    char axcindicate;  // squelch/AFC status indicator: ' ' - no signal; '*' - has signal; '>', '<' - signal tuned by AFC
    unsigned char afc; //0 - AFC disabled; 1 - minimal AFC; 2 - more aggressive AFC and so on to 255
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

device_t* devices;
int device_count;
volatile int device_opened = 0;
int rtlsdr_buffers = 10;
#ifdef _WIN32
int avx;
#endif
int foreground = 0, do_syslog = 1, shout_metadata_delay = 3;
static volatile int do_exit = 0;
bool use_localtime = false;
#ifdef NFM
float alpha = exp(-1.0f/(WAVE_RATE * 2e-4));
enum fm_demod_algo {
    FM_FAST_ATAN2,
    FM_QUADRI_DEMOD
};
enum fm_demod_algo fm_demod = FM_FAST_ATAN2;
#endif

void error() {
#ifdef _WIN32
    system("pause");
#endif
    exit(1);
}


int atomic_inc(volatile int *pv)
{
#ifdef _WIN32
    return InterlockedIncrement((volatile LONG *)pv);
#else
    return __sync_fetch_and_add(pv, 1);
#endif
}

int atomic_dec(volatile int *pv)
{
#ifdef _WIN32
    return InterlockedDecrement((volatile LONG *)pv);
#else
    return __sync_fetch_and_sub(pv, 1);
#endif
}

int atomic_get(volatile int *pv)
{
#ifdef _WIN32
    return InterlockedCompareExchange((volatile LONG *)pv, 0, 0);
#else
    return __sync_fetch_and_add(pv, 0);
#endif
}

void log(int priority, const char *format, ...) {
    va_list args;
    va_start(args, format);
#ifdef _WIN32
    if(!foreground) 
        vprintf(format, args);
#else
    if(do_syslog)
        vsyslog(priority, format, args);
    else if(foreground)
        vprintf(format, args);
#endif
    va_end(args);
}

void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
    if(do_exit) return;
    device_t *dev = (device_t*)ctx;
    memcpy(dev->buffer + dev->bufe, buf, len);
    if (dev->bufe == 0) {
        memcpy(dev->buffer + BUF_SIZE, buf, FFT_SIZE * 2);
    }
    dev->bufe = dev->bufe + len;
    if (dev->bufe == BUF_SIZE) dev->bufe = 0;
}

#ifdef _WIN32
BOOL WINAPI sighandler(int sig) {
    if (CTRL_C_EVENT == sig) {
        log(LOG_NOTICE, "Got signal %d, exiting\n", sig);
        do_exit = 1;
        return TRUE;
    }
    return FALSE;
}
#else
void sighandler(int sig) {
    log(LOG_NOTICE, "Got signal %d, exiting\n", sig);
    do_exit = 1;
}
#endif

#ifdef _WIN32
void rtlsdr_exec(void* params) {
#else
void* rtlsdr_exec(void* params) {
#endif
    int r;
    device_t *dev = (device_t*)params;

    dev->rtlsdr = NULL;
    rtlsdr_open(&dev->rtlsdr, dev->device);

    if (NULL == dev->rtlsdr) {
        log(LOG_ERR, "Failed to open rtlsdr device #%d.\n", dev->device);
        error();
        return NULL;
    }
    r = rtlsdr_set_sample_rate(dev->rtlsdr, SOURCE_RATE);
    if (r < 0) log(LOG_ERR, "Failed to set sample rate for device #%d. Error %d.\n", dev->device, r);
    r = rtlsdr_set_center_freq(dev->rtlsdr, dev->centerfreq);
    if (r < 0) log(LOG_ERR, "Failed to set center freq for device #%d. Error %d.\n", dev->device, r);
    r = rtlsdr_set_freq_correction(dev->rtlsdr, dev->correction);
    if (r < 0 && r != -2 ) log(LOG_ERR, "Failed to set freq correction for device #%d. Error %d.\n", dev->device, r);

    if(dev->gain == AUTO_GAIN) {
        r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr, 0);
        if (r < 0)
            log(LOG_ERR, "Failed to set automatic gain for device #%d. Error %d.\n", dev->device, r);
        else
            log(LOG_INFO, "Device #%d: gain set to automatic\n", dev->device);
    } else {
        r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr, 1);
        r |= rtlsdr_set_tuner_gain(dev->rtlsdr, dev->gain);
        if (r < 0)
            log(LOG_ERR, "Failed to set gain to %0.2f for device #%d. Error %d.\n", (float)rtlsdr_get_tuner_gain(dev->rtlsdr) / 10.0, dev->device, r);
        else
            log(LOG_INFO, "Device #%d: gain set to %0.2f dB\n", dev->device, (float)rtlsdr_get_tuner_gain(dev->rtlsdr) / 10.0);
    }

    r = rtlsdr_set_agc_mode(dev->rtlsdr, 0);
    if (r < 0) log(LOG_ERR, "Failed to disable AGC for device #%d. Error %d.\n", dev->device, r);
    rtlsdr_reset_buffer(dev->rtlsdr);
    log(LOG_INFO, "Device %d started.\n", dev->device);
    atomic_inc(&device_opened);
    dev->failed = 0;
    if(rtlsdr_read_async(dev->rtlsdr, rtlsdr_callback, params, rtlsdr_buffers, 320000) < 0) {
        log(LOG_WARNING, "Device #%d: async read failed, disabling\n", dev->device);
        dev->failed = 1;
        atomic_dec(&device_opened);
    }
    return 0;
}

void shout_setup(icecast_data *icecast) {
    int ret;
    shout_t * shouttemp = shout_new();
    if (shouttemp == NULL) {
        printf("cannot allocate\n");
    }
    if (shout_set_host(shouttemp, icecast->hostname) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_protocol(shouttemp, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_port(shouttemp, icecast->port) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    char mp[100];
#ifdef _WIN32
    sprintf_s(mp, 80, "/%s", icecast->mountpoint);
#else
    sprintf(mp, "/%s", icecast->mountpoint);
#endif
    if (shout_set_mount(shouttemp, mp) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_user(shouttemp, icecast->username) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_password(shouttemp, icecast->password) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_format(shouttemp, SHOUT_FORMAT_MP3) != SHOUTERR_SUCCESS){
        shout_free(shouttemp); return;
    }
    if(icecast->name && shout_set_name(shouttemp, icecast->name) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if(icecast->genre && shout_set_genre(shouttemp, icecast->genre) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    char samplerates[20];
#ifdef _WIN32
    sprintf_s(samplerates, 15, "%d", MP3_RATE);
#else
    sprintf(samplerates, "%d", MP3_RATE);
#endif
    shout_set_audio_info(shouttemp, SHOUT_AI_SAMPLERATE, samplerates);
    shout_set_audio_info(shouttemp, SHOUT_AI_CHANNELS, "1");

    if (shout_set_nonblocking(shouttemp, 1) != SHOUTERR_SUCCESS) {
        log(LOG_ERR, "Error setting non-blocking mode: %s\n", shout_get_error(shouttemp));
        return;
    }
    ret = shout_open(shouttemp);
    if (ret == SHOUTERR_SUCCESS)
        ret = SHOUTERR_CONNECTED;

    if (ret == SHOUTERR_BUSY)
        log(LOG_NOTICE, "Connecting to %s:%d/%s...\n", 
            icecast->hostname, icecast->port, icecast->mountpoint);

    while (ret == SHOUTERR_BUSY) {
        usleep(10000);
        ret = shout_get_connected(shouttemp);
    }
 
    if (ret == SHOUTERR_CONNECTED) {
        log(LOG_NOTICE, "Connected to %s:%d/%s\n", 
            icecast->hostname, icecast->port, icecast->mountpoint);
        SLEEP(100);
        icecast->shout = shouttemp;
    } else {
        log(LOG_WARNING, "Could not connect to %s:%d/%s\n",
            icecast->hostname, icecast->port, icecast->mountpoint);
        shout_free(shouttemp);
        return;
    }
}

lame_t airlame_init() {
    lame_t lame = lame_init();
    if (!lame) {
        log(LOG_WARNING, "lame_init failed\n");
        return NULL;
    }

    lame_set_in_samplerate(lame, WAVE_RATE);
    lame_set_VBR(lame, vbr_mtrh);
    lame_set_brate(lame, 16);
    lame_set_quality(lame, 7);
    lame_set_out_samplerate(lame, MP3_RATE);
    lame_set_num_channels(lame, 1);
    lame_set_mode(lame, MONO);
    lame_init_params(lame);
    return lame;
}

class LameTone
{
    unsigned char* _data;
    int _bytes;

public:
    LameTone(int msec, unsigned int hz = 0) : _data(NULL), _bytes(0) {
         _data = (unsigned char *)malloc(LAMEBUF_SIZE);
         if (!_data) {
             log(LOG_WARNING, "LameTone: can't alloc %u bytes\n", LAMEBUF_SIZE);
             return;
         }

         int samples = (msec * WAVE_RATE) / 1000;
         float *buf = (float *)malloc(samples * sizeof(float));
         if (!buf) {
             log(LOG_WARNING, "LameTone: can't alloc %u samples\n", samples);
             return;
         }

         if (hz > 0) {
             const float period = 1.0 / (float)hz;
             const float sample_time = 1.0 / (float)WAVE_RATE;
             float t = 0;
             for (int i = 0; i < samples; ++i, t+= sample_time) {
                 buf[i] = 0.9 * sinf(t * 2.0 * M_PI / period);
             }
         } else
             memset(buf, 0, samples * sizeof(float));

         lame_t lame = airlame_init();
         if (lame) {
             _bytes = lame_encode_buffer_ieee_float(lame, buf, NULL, samples, _data, LAMEBUF_SIZE);
             if (_bytes > 0) {
                 int flush_ofs = _bytes;
                 if (flush_ofs&0x1f)
                     flush_ofs+= 0x20 - (flush_ofs&0x1f);
                 if (flush_ofs < LAMEBUF_SIZE) {
                     int flush_bytes = lame_encode_flush(lame, _data + flush_ofs, LAMEBUF_SIZE - flush_ofs);
                     if (flush_bytes > 0) {
                         memmove(_data + _bytes, _data + flush_ofs, flush_bytes);
                         _bytes+= flush_bytes;
                     }
                 }
             }
             else
                 log(LOG_WARNING, "lame_encode_buffer_ieee_float: %d\n", _bytes);
             lame_close(lame);
         }
         free(buf);
    }

    ~LameTone() {
        if (_data)
            free(_data);
    }

    int write(FILE *f) {
        if (!_data || _bytes<=0)
            return 1;

        if(fwrite(_data, 1, _bytes, f) != (unsigned int)_bytes) {
            log(LOG_WARNING, "LameTone: failed to write %d bytes\n", _bytes);
            return -1;
        }

        return 0;
    }
};

static int fdata_open(file_data *fdata, const char *filename) {
    fdata->f = fopen(filename, fdata->append ? "a+" : "w");
    if(fdata->f == NULL)
        return -1;

    struct stat st = {0};
    if (!fdata->append || fstat(fileno(fdata->f), &st)!=0 || st.st_size == 0) {
        log(LOG_INFO, "Writing to %s\n", filename);
        return 0;
    }
    log(LOG_INFO, "Appending from pos %llu to %s\n", (unsigned long long)st.st_size, filename);

    //fill missing space with marker tones
    LameTone lt_a(120, 2222);
    LameTone lt_b(120, 1111);
    LameTone lt_c(120, 555);

    int r = lt_a.write(fdata->f);
    if (r==0) r = lt_b.write(fdata->f);
    if (r==0) r = lt_c.write(fdata->f);
    if (fdata->continuous) {
        time_t now = time(NULL);
        if (now > st.st_mtime ) {
            time_t delta = now - st.st_mtime;
            if (delta > 3600) {
                log(LOG_WARNING, "Too big time difference: %llu sec, limiting to one hour\n", (unsigned long long)delta);
                delta = 3600;
            }
            LameTone lt_silence(1000);
            for (; (r==0 && delta > 1); --delta)
                r = lt_silence.write(fdata->f);
        }
    }
    if (r==0) r = lt_c.write(fdata->f);
    if (r==0) r = lt_b.write(fdata->f);
    if (r==0) r = lt_a.write(fdata->f);

    if (r<0) fseek(fdata->f, st.st_size, SEEK_SET);
    return 0;
}

unsigned char lamebuf[LAMEBUF_SIZE];
void process_outputs(channel_t *channel, int cur_scan_freq) {
    int bytes = lame_encode_buffer_ieee_float(channel->lame, channel->waveout, NULL, WAVE_BATCH, lamebuf, LAMEBUF_SIZE);
    if (bytes < 0) {
        log(LOG_WARNING, "lame_encode_buffer_ieee_float: %d\n", bytes);
        return;
    } else if (bytes == 0)
        return;
    for (int k = 0; k < channel->output_count; k++) {
        if(channel->outputs[k].enabled == false) continue;
        if(channel->outputs[k].type == O_ICECAST) {
            icecast_data *icecast = (icecast_data *)(channel->outputs[k].data);
            if(icecast->shout == NULL) continue;
            int ret = shout_send(icecast->shout, lamebuf, bytes);
            if (ret != SHOUTERR_SUCCESS || shout_queuelen(icecast->shout) > MAX_SHOUT_QUEUELEN) {
                if (shout_queuelen(icecast->shout) > MAX_SHOUT_QUEUELEN)
                    log(LOG_WARNING, "Exceeded max backlog for %s:%d/%s, disconnecting\n",
                        icecast->hostname, icecast->port, icecast->mountpoint);
                // reset connection
                log(LOG_WARNING, "Lost connection to %s:%d/%s\n",
                    icecast->hostname, icecast->port, icecast->mountpoint);
                shout_close(icecast->shout);
                shout_free(icecast->shout);
                icecast->shout = NULL;
            } else if(icecast->send_scan_freq_tags && cur_scan_freq >= 0) {
                shout_metadata_t *meta = shout_metadata_new();
                char description[32];
                if(channel->labels[cur_scan_freq] != NULL)
                    shout_metadata_add(meta, "song", channel->labels[cur_scan_freq]);
                else {
                    snprintf(description, sizeof(description), "%.3f MHz", channel->freqlist[cur_scan_freq] / 1000000.0);
                    shout_metadata_add(meta, "song", description);
                }
                shout_set_metadata(icecast->shout, meta);
                shout_metadata_free(meta);
            }
        } else if(channel->outputs[k].type == O_FILE) {
            file_data *fdata = (file_data *)(channel->outputs[k].data);
            if(fdata->continuous == false && channel->axcindicate == ' ' && channel->outputs[k].active == false) continue;
            time_t t = time(NULL);
            struct tm *tmp;
            if(use_localtime)
                 tmp = localtime(&t);
            else
                 tmp = gmtime(&t);

            char suffix[32];
            if(strftime(suffix, sizeof(suffix), "_%Y%m%d_%H.mp3", tmp) == 0) {
                log(LOG_NOTICE, "strftime returned 0\n");
                continue;
            }
            if(fdata->suffix == NULL || strcmp(suffix, fdata->suffix)) {    // need to open new file
                fdata->suffix = strdup(suffix);
                char *filename = (char *)malloc(strlen(fdata->dir) + strlen(fdata->prefix) + strlen(fdata->suffix) + 2);
                if(filename == NULL) {
                    log(LOG_WARNING, "process_outputs: cannot allocate memory, output disabled\n");
                    channel->outputs[k].enabled = false;
                    continue;
                }
                sprintf(filename, "%s/%s%s", fdata->dir, fdata->prefix, fdata->suffix);
                if(fdata->f != NULL) {
                    //todo: finalize file stream with lame_encode_flush_nogap
                    fclose(fdata->f);
                    fdata->f = NULL;
                }
                int r = fdata_open(fdata, filename);
                free(filename);
                if (r<0) {
                    log(LOG_WARNING, "Cannot open output file %s (%s), output disabled\n", filename, strerror(errno));
                    channel->outputs[k].enabled = false;
                    continue;
                }
            }
// bytes is signed, but we've checked for negative values earlier
// so it's save to ignore the warning here
#pragma GCC diagnostic ignored "-Wsign-compare"
            if(fwrite(lamebuf, 1, bytes, fdata->f) < bytes) {
#pragma GCC diagnostic warning "-Wsign-compare"
                if(ferror(fdata->f))
                    log(LOG_WARNING, "Cannot write to %s/%s%s (%s), output disabled\n", 
                        fdata->dir, fdata->prefix, fdata->suffix, strerror(errno));
                else
                    log(LOG_WARNING, "Short write on %s/%s%s, output disabled\n", 
                        fdata->dir, fdata->prefix, fdata->suffix);
                fclose(fdata->f);
                fdata->f = NULL;
                channel->outputs[k].enabled = false;
            }
            channel->outputs[k].active = (channel->axcindicate != ' ');
        }
    }
}

void tag_queue_put(device_t *dev, int freq, struct timeval tv) {
    pthread_mutex_lock(&dev->tag_queue_lock);
    dev->tq_head++; dev->tq_head %= TAG_QUEUE_LEN;
    if(dev->tq_head == dev->tq_tail) {
        log(LOG_WARNING, "tag_queue_put: queue overrun");
        dev->tq_tail++;
    }
    dev->tag_queue[dev->tq_head].freq = freq;
    memcpy(&dev->tag_queue[dev->tq_head].tv, &tv, sizeof(struct timeval));
    pthread_mutex_unlock(&dev->tag_queue_lock);
}

void tag_queue_get(device_t *dev, struct freq_tag *tag) {
    int i;

    if(!tag) return;
    pthread_mutex_lock(&dev->tag_queue_lock);
    if(dev->tq_head == dev->tq_tail) {      /* empty queue */
        tag->freq = -1;
    } else {
// read queue entry at pos tq_tail+1 without dequeueing it
        i = dev->tq_tail+1; i %= TAG_QUEUE_LEN;
        tag->freq = dev->tag_queue[i].freq;
        memcpy(&tag->tv, &dev->tag_queue[i].tv, sizeof(struct timeval));
    }
    pthread_mutex_unlock(&dev->tag_queue_lock);
}

void tag_queue_advance(device_t *dev) {
    pthread_mutex_lock(&dev->tag_queue_lock);
    dev->tq_tail++; dev->tq_tail %= TAG_QUEUE_LEN;
    pthread_mutex_unlock(&dev->tag_queue_lock);
}

#ifndef _WIN32
pthread_cond_t      mp3_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mp3_mutex = PTHREAD_MUTEX_INITIALIZER;
void* output_thread(void* params) {
    struct freq_tag tag;
    struct timeval tv;
    int new_freq = -1;

    while (!do_exit) {
        pthread_cond_wait(&mp3_cond, &mp3_mutex);
        for (int i = 0; i < device_count; i++) {
            device_t* dev = devices + i;
            if (!dev->failed && dev->waveavail) {
                dev->waveavail = 0;
                if(dev->mode == R_SCAN) {
                    tag_queue_get(dev, &tag);
                    if(tag.freq >= 0) {
                        tag.tv.tv_sec += shout_metadata_delay;
                        gettimeofday(&tv, NULL);
                        if(tag.tv.tv_sec < tv.tv_sec || (tag.tv.tv_sec == tv.tv_sec && tag.tv.tv_usec <= tv.tv_usec)) {
                            new_freq = tag.freq;
                            tag_queue_advance(dev);
                        }
                    }
                }
                for (int j = 0; j < dev->channel_count; j++) {
                    channel_t* channel = devices[i].channels + j;
                    process_outputs(channel, new_freq);
                    memcpy(channel->waveout, channel->waveout + WAVE_BATCH, AGC_EXTRA * 4);
                }
            }
// make sure we don't carry new_freq value to the next receiver which might be working
// in multichannel mode
            new_freq = -1;
        }
    }
    return 0;
}
#endif

void* controller_thread(void* params) {
    device_t *dev = (device_t*)params;
    int i = 0;
    int consecutive_squelch_off = 0;
    struct timeval tv;

    if(dev->channels[0].freq_count < 2) return 0;
    while(!do_exit) {
        SLEEP(200);
        if(dev->channels[0].axcindicate == ' ') {
            if(consecutive_squelch_off < 10) {
                consecutive_squelch_off++;
            } else {
                i++; i %= dev->channels[0].freq_count;
                dev->channels[0].frequency = dev->channels[0].freqlist[i];
                dev->centerfreq = dev->channels[0].freqlist[i] + 2 * (double)(SOURCE_RATE / FFT_SIZE);
                rtlsdr_set_center_freq(dev->rtlsdr, dev->centerfreq);
            }
        } else {
            if(consecutive_squelch_off == 10) {
                if(i != dev->last_frequency) {
// squelch has just opened on a new frequency - we might need to update outputs' metadata
                    gettimeofday(&tv, NULL);
                    tag_queue_put(dev, i, tv);
                    dev->last_frequency = i;
                }
            }
            consecutive_squelch_off = 0;
        }
    }
    return 0;
}

// reconnect as required
#ifdef _WIN32
void icecast_check(void* params) {
#else
void* icecast_check(void* params) {
#endif
    while (!do_exit) {
        SLEEP(10000);
        for (int i = 0; i < device_count; i++) {
            device_t* dev = devices + i;
            for (int j = 0; j < dev->channel_count; j++) {
                for (int k = 0; k < dev->channels[j].output_count; k++) {
                    if(dev->channels[j].outputs[k].type != O_ICECAST)
                        continue;
                    icecast_data *icecast = (icecast_data *)(dev->channels[j].outputs[k].data);
                    if(dev->failed) {
                        if(icecast->shout) {
                            log(LOG_WARNING, "Device #%d failed, disconnecting stream %s:%d/%s\n",
                                i, icecast->hostname, icecast->port, icecast->mountpoint);
                            shout_close(icecast->shout);
                            shout_free(icecast->shout);
                            icecast->shout = NULL;
                        }
                    } else {
                        if (icecast->shout == NULL){
                            log(LOG_NOTICE, "Trying to reconnect to %s:%d/%s...\n",
                                icecast->hostname, icecast->port, icecast->mountpoint);
                            shout_setup(icecast);
                        }
                    }
                }
            }
        }
    }
#ifndef _WIN32
    return 0;
#endif
}

#ifdef NFM
void multiply(float ar, float aj, float br, float bj, float *cr, float *cj)
{
    *cr = ar*br - aj*bj;
    *cj = aj*br + ar*bj;
}

float fast_atan2(float y, float x)
{
    float yabs, angle;
    float pi4=M_PI_4, pi34=3*M_PI_4;
    if (x==0.0f && y==0.0f) {
            return 0;
    }
    yabs = y;
    if (yabs < 0.0f) {
            yabs = -yabs;
    }
    if (x >= 0.0f) {
            angle = pi4  - pi4 * (x-yabs) / (x+yabs);
    } else {
            angle = pi34 - pi4 * (x+yabs) / (yabs-x);
    }  
    if (y < 0.0f) {
            return -angle;
    }
    return angle;
}

float polar_disc_fast(float ar, float aj, float br, float bj)
{
    float cr, cj;
    multiply(ar, aj, br, -bj, &cr, &cj);
    return (float)(fast_atan2(cj, cr) * M_1_PI);
}

float fm_quadri_demod(float ar, float aj, float br, float bj) {
    return (float)((br*aj - ar*bj)/(ar*ar + aj*aj + 1.0f) * M_1_PI);
}

#endif

class AFC
{
    const char _prev_axcindicate;

#ifdef USE_BCM_VC
    float square(const GPU_FFT_COMPLEX *fft_results, int index)
    {
        return fft_results[index].re * fft_results[index].re + fft_results[index].im * fft_results[index].im;
    }
#else
    float square(const fftwf_complex *fft_results, int index)
    {
        return fft_results[index][0] * fft_results[index][0] + fft_results[index][1] * fft_results[index][1];
    }
#endif
    template <class FFT_RESULTS, int STEP>
        int check(const FFT_RESULTS* fft_results, const int base, const float base_value, unsigned char afc)
    {
        float threshold = 0;
        int bin;
        for (bin = base;; bin+= STEP) {
            if (STEP < 0) {
              if (bin < -STEP)
                  break;

            } else if ( (bin + STEP) >= FFT_SIZE) 
                  break;

            const float value = square(fft_results, bin + STEP);
            if (value <= base_value) 
                break;
             
            if (base == bin) {
                threshold = (value - base_value) / (float)afc;
            } else {
                if ((value - base_value) < threshold)
                    break;

                threshold+= threshold / 10.0;
            }
        }
        return bin;
    }

public:
    AFC(device_t* dev, int index) : _prev_axcindicate(dev->channels[index].axcindicate)
    {
    }

    template <class FFT_RESULTS>
        void finalize(device_t* dev, int index, const FFT_RESULTS* fft_results)
    {
        channel_t *channel = &dev->channels[index];
        if (channel->afc==0)
            return;

        const char axcindicate = channel->axcindicate;
        if (axcindicate != ' ' && _prev_axcindicate == ' ') {
            const int base = dev->base_bins[index];
            const float base_value = square(fft_results, base);
            int bin = check<FFT_RESULTS, -1>(fft_results, base, base_value, channel->afc);
            if (bin == base)
                bin = check<FFT_RESULTS, 1>(fft_results, base, base_value, channel->afc);

             if (dev->bins[index] != bin) {
#ifdef AFC_LOGGING
                 log(LOG_INFO, "AFC device=%d channel=%d: base=%d prev=%d now=%d\n", dev->device, index, base, dev->bins[index], bin);
#endif
                 dev->bins[index] = bin;
                 if ( bin > base )
                     channel->axcindicate = '>';
                 else if ( bin < base )
                     channel->axcindicate = '<';
             }
        }
        else if (axcindicate == ' ' && _prev_axcindicate != ' ')
            dev->bins[index] = dev->base_bins[index];
    }
};

void demodulate() {

    // initialize fft engine
#ifndef USE_BCM_VC
    fftwf_plan fft;
    fftwf_complex* fftin;
    fftwf_complex* fftout;
    fftin = fftwf_alloc_complex(FFT_SIZE);
    fftout = fftwf_alloc_complex(FFT_SIZE);
    fft = fftwf_plan_dft_1d(FFT_SIZE, fftin, fftout, FFTW_FORWARD, FFTW_MEASURE);
#else
    int mb = mbox_open();
    struct GPU_FFT *fft;
    int ret = gpu_fft_prepare(mb, FFT_SIZE_LOG, GPU_FFT_FWD, FFT_BATCH, &fft);
    switch (ret) {
        case -1: log(LOG_CRIT, "Unable to enable V3D. Please check your firmware is up to date.\n"); error();
        case -2: log(LOG_CRIT, "log2_N=%d not supported.  Try between 8 and 17.\n", FFT_SIZE_LOG); error();
        case -3: log(LOG_CRIT, "Out of memory.  Try a smaller batch or increase GPU memory.\n"); error();
    }
#endif

#ifdef NFM
    float rotated_r, rotated_j;
#endif
    ALIGN float ALIGN2 levels[256];
    for (int i=0; i<256; i++) {
        levels[i] = i-127.5f;
    }

    // initialize fft window
    // blackman 7
    // the whole matrix is computed
    ALIGN float ALIGN2 window[FFT_SIZE * 2];
    const double a0 = 0.27105140069342f;
    const double a1 = 0.43329793923448f;   const double a2 = 0.21812299954311f;
    const double a3 = 0.06592544638803f;   const double a4 = 0.01081174209837f;
    const double a5 = 0.00077658482522f;   const double a6 = 0.00001388721735f;

    for (int i = 0; i < FFT_SIZE; i++) {
        double x = a0 - (a1 * cos((2.0 * M_PI * i) / (FFT_SIZE-1)))
            + (a2 * cos((4.0 * M_PI * i) / (FFT_SIZE - 1)))
            - (a3 * cos((6.0 * M_PI * i) / (FFT_SIZE - 1)))
            + (a4 * cos((8.0 * M_PI * i) / (FFT_SIZE - 1)))
            - (a5 * cos((10.0 * M_PI * i) / (FFT_SIZE - 1)))
            + (a6 * cos((12.0 * M_PI * i) / (FFT_SIZE - 1)));
        window[i * 2] = window[i * 2 + 1] = (float)x;
    }

    // speed2 = number of bytes per wave sample (x 2 for I and Q)
    int speed2 = (SOURCE_RATE * 2) / WAVE_RATE;
    int device_num = 0;
    while (true) {

        if(do_exit) {
#ifdef USE_BCM_VC
            log(LOG_INFO, "Freeing GPU memory\n");
            gpu_fft_release(fft);
#endif
            return;
        }

        device_t* dev = devices + device_num;
        int available = dev->bufe - dev->bufs;
        if (dev->bufe < dev->bufs) {
            available = (BUF_SIZE - dev->bufe) + dev->bufs;
        }

        if(atomic_get(&device_opened)==0) {
            log(LOG_ERR, "All receivers failed, exiting\n");
            do_exit = 1;
            continue;
        }
        if (dev->failed) {
            // move to next device
            device_num = (device_num + 1) % device_count;
            continue;
        } else if (available < speed2 * FFT_BATCH + FFT_SIZE * 2) {
            // move to next device
            device_num = (device_num + 1) % device_count;
            SLEEP(10);
            continue;
        }

#if defined USE_BCM_VC
        sample_fft_arg sfa = {FFT_SIZE / 4, fft->in};
        for (int i = 0; i < FFT_BATCH; i++) {
            samplefft(&sfa, dev->buffer + dev->bufs + i * speed2, window, levels);
            sfa.dest+= fft->step;
        }
#elif defined __arm__
        for (int i = 0; i < FFT_SIZE; i++) {
            unsigned char* buf2 = dev->buffer + dev->bufs + i * 2;
            fftin[i][0] = levels[*(buf2)] * window[i*2];
            fftin[i][1] = levels[*(buf2+1)] * window[i*2];
        }
#elif defined _WIN32
        // process 4 rtl samples (16 bytes)
        if (avx) {
            for (int i = 0; i < FFT_SIZE; i += 4) {
                unsigned char* buf2 = dev->buffer + dev->bufs + i * 2;
                __m256 a = _mm256_set_ps(levels[*(buf2+7)], levels[*(buf2+6)], levels[*(buf2+5)], levels[*(buf2+4)], levels[*(buf2+3)], levels[*(buf2+2)], levels[*(buf2+1)], levels[*(buf2)]);
                __m256 b = _mm256_load_ps(&window[i * 2]);
                a = _mm256_mul_ps(a, b);
                _mm_prefetch((const CHAR *)&window[i * 2 + 128], _MM_HINT_T0);
                _mm_prefetch((const CHAR *)buf2 + 128, _MM_HINT_T0);
                _mm256_store_ps(&fftin[i][0], a);
            }
        } else {
            for (int i = 0; i < FFT_SIZE; i += 2) {
                unsigned char* buf2 = dev->buffer + dev->bufs + i * 2;
                __m128 a = _mm_set_ps(levels[*(buf2 + 3)], levels[*(buf2 + 2)], levels[*(buf2 + 1)], levels[*(buf2)]);
                __m128 b = _mm_load_ps(&window[i * 2]);
                a = _mm_mul_ps(a, b);
                _mm_store_ps(&fftin[i][0], a);
            }
        }
#else /* x86 */
        for (int i = 0; i < FFT_SIZE; i += 2) {
            unsigned char* buf2 = dev->buffer + dev->bufs + i * 2;
            __m128 a = _mm_set_ps(levels[*(buf2 + 3)], levels[*(buf2 + 2)], levels[*(buf2 + 1)], levels[*(buf2)]);
            __m128 b = _mm_load_ps(&window[i * 2]);
            a = _mm_mul_ps(a, b);
            _mm_store_ps(&fftin[i][0], a);
        }
#endif
#ifndef _WIN32
        // allow mp3 encoding thread to run while waiting for FFT to finish
        pthread_cond_signal(&mp3_cond);
#endif
#ifdef USE_BCM_VC
        gpu_fft_execute(fft);
#else
        fftwf_execute(fft);
#endif

#ifdef USE_BCM_VC
        for (int i = 0; i < dev->channel_count; i++) {
            float *wavein = dev->channels[i].wavein + dev->waveend;
            __builtin_prefetch(wavein, 1);
            const int bin = dev->bins[i];
            const GPU_FFT_COMPLEX *fftout = fft->out + bin;
            for (int j = 0; j < FFT_BATCH; j++, ++wavein, fftout+= fft->step)
                *wavein = sqrtf(fftout->im * fftout->im + fftout->re * fftout->re);
        }
        

# ifdef NFM
        for (int j = 0; j < dev->channel_count; j++) {
            if(dev->channels[j].modulation == MOD_NFM) {
                struct GPU_FFT_COMPLEX *ptr = fft->out;
                for (int job = 0; job < FFT_BATCH; job++) {
                    dev->channels[j].complex_samples[2*(dev->waveend+job)] = ptr[dev->bins[j]].re;
                    dev->channels[j].complex_samples[2*(dev->waveend+job)+1] = ptr[dev->bins[j]].im;
                    ptr += fft->step;
                }
            }
        }
# endif // NFM
#else
        for (int j = 0; j < dev->channel_count; j++) {
            dev->channels[j].wavein[dev->waveend] =
              sqrtf(fftout[dev->bins[j]][0] * fftout[dev->bins[j]][0] + fftout[dev->bins[j]][1] * fftout[dev->bins[j]][1]);
# ifdef NFM
            if(dev->channels[j].modulation == MOD_NFM) {
                dev->channels[j].complex_samples[2*dev->waveend] = fftout[dev->bins[j]][0];
                dev->channels[j].complex_samples[2*dev->waveend+1] = fftout[dev->bins[j]][1];
            }
# endif // NFM
        }
#endif // USE_BCM_VC

        dev->waveend += FFT_BATCH;
        
        if (dev->waveend >= WAVE_BATCH + AGC_EXTRA) {
            if (foreground) {
                GOTOXY(0, device_num * 17 + dev->row + 3);
            }
            for (int i = 0; i < dev->channel_count; i++) {
                AFC afc(dev, i);
                channel_t* channel = dev->channels + i;
                for (int j = AGC_EXTRA; j < WAVE_BATCH + AGC_EXTRA; j++) {
                    // auto noise floor
                    if (channel->sqlevel < 0 && j % 16 == 0) {
                        channel->agcmin = channel->agcmin * 0.97f + min(channel->agcavgslow, channel->agcmin) * 0.03f + 0.0001f;
                    }

                    // average power
                    channel->agcavgslow = channel->agcavgslow * 0.99f + channel->wavein[j] * 0.01f;

                    float sqlevel = channel->sqlevel > 0 ? (float)channel->sqlevel : 3.0f * channel->agcmin;
                    if (channel->agcsq > 0) {
                        channel->agcsq = max(channel->agcsq - 1, 1);
                        if (channel->agcsq == 1 && channel->agcavgslow > sqlevel) {
                            channel->agcsq = -AGC_EXTRA * 2;
                            channel->axcindicate = '*';
                            if(channel->modulation == MOD_AM) {
                            // fade in
                                for (int k = j - AGC_EXTRA; k < j; k++) {
                                    if (channel->wavein[k] > sqlevel) {
                                        channel->agcavgfast = channel->agcavgfast * 0.98f + channel->wavein[k] * 0.02f;
                                    }
                                }
                            }
                        }
                    } else {
                        if (channel->wavein[j] > sqlevel) {
                            if(channel->modulation == MOD_AM)
                                channel->agcavgfast = channel->agcavgfast * 0.995f + channel->wavein[j] * 0.005f;
                            channel->agclow = 0;
                        } else {
                            channel->agclow++;
                        }
                        channel->agcsq = min(channel->agcsq + 1, -1);
                        sqlevel = channel->sqlevel > 0 ? (float)channel->sqlevel : 2.4f * channel->agcmin;
                        if ((channel->agcsq == -1 && channel->agcavgslow < sqlevel) || channel->agclow == AGC_EXTRA - 12) {
                            channel->agcsq = AGC_EXTRA * 2;
                            channel->axcindicate = ' ';
                            if(channel->modulation == MOD_AM) {
                                // fade out
                                for (int k = j - AGC_EXTRA + 1; k < j; k++) {
                                    channel->waveout[k] = channel->waveout[k - 1] * 0.94f;
                                }
                            }
                        }
                    }
                    if(channel->agcsq != -1) {
                        channel->waveout[j] = 0;
                    } else {
                        if(channel->modulation == MOD_AM) {
                            channel->waveout[j] = (channel->wavein[j - AGC_EXTRA] - channel->agcavgfast) / (channel->agcavgfast * 2.5f);
                            if (abs(channel->waveout[j]) > 0.8f) {
                                channel->waveout[j] *= 0.85f;
                                channel->agcavgfast *= 1.15f;
                            }
                        }
#ifdef NFM
                        else {    // NFM
                            multiply(channel->complex_samples[2*(j - AGC_EXTRA)], channel->complex_samples[2*(j - AGC_EXTRA)+1],
// FIXME: use j instead of wavecnt?
                              channel->timeref_cos[channel->wavecnt],
                              channel->timeref_nsin[channel->wavecnt],
                              &rotated_r,
                              &rotated_j);
                            if(fm_demod == FM_FAST_ATAN2) {
                                channel->waveout[j] = polar_disc_fast(rotated_r, rotated_j, channel->pr, channel->pj);
                            } else if(fm_demod == FM_QUADRI_DEMOD) {
                                channel->waveout[j] = fm_quadri_demod(rotated_r, rotated_j, channel->pr, channel->pj);
                            }
                            channel->pr = rotated_r;
                            channel->pj = rotated_j;
// de-emphasis IIR + DC blocking
                            channel->agcavgfast = channel->agcavgfast * 0.995f + channel->waveout[j] * 0.005f;
                            channel->waveout[j] -= channel->agcavgfast;
                            channel->waveout[j] = channel->waveout[j] * (1.0f - channel->alpha) + channel->waveout[j-1] * channel->alpha;
                        }
#endif // NFM
                    }
#ifdef NFM
                    if(channel->modulation == MOD_NFM) 
                        channel->wavecnt = (channel->wavecnt + 1) % WAVE_RATE;
#endif // NFM
                }
#ifdef _WIN32
                process_outputs(channel);
                memmove(channel->waveout, channel->waveout + WAVE_BATCH, AGC_EXTRA * 4);
#endif
                memmove(channel->wavein, channel->wavein + WAVE_BATCH, (dev->waveend - WAVE_BATCH) * 4);
#ifdef NFM
                if(channel->modulation == MOD_NFM)
                    memmove(channel->complex_samples, channel->complex_samples + 2 * WAVE_BATCH, (dev->waveend - WAVE_BATCH) * 4 * 2);
#endif

#ifdef USE_BCM_VC
                afc.finalize(dev, i, fft->out);
#else
                afc.finalize(dev, i, fftout);
#endif

                if (foreground) {
                    if(dev->mode == R_SCAN)
                        printf("%4.0f/%3.0f%c %7.3f", channel->agcavgslow, channel->agcmin, channel->axcindicate, (dev->channels[0].frequency / 1000000.0));
                    else
                        printf("%4.0f/%3.0f%c", channel->agcavgslow, channel->agcmin, channel->axcindicate);
                    fflush(stdout);
                }
            }
            dev->waveavail = 1;
            dev->waveend -= WAVE_BATCH;
            dev->row++;
            if (dev->row == 12) {
                dev->row = 0;
            }
        }

        dev->bufs += speed2 * FFT_BATCH;
        if (dev->bufs >= BUF_SIZE) dev->bufs -= BUF_SIZE;
#ifndef _WIN32
        // always rotate to next device on unix
        device_num = (device_num + 1) % device_count;
#endif
    }
}

void usage() {
    cout<<"Usage: rtl_airband [options] [-c <config_file_path>]\n\
\t-h\t\t\tDisplay this help text\n\
\t-f\t\t\tRun in foreground, display textual waterfalls\n";
#ifdef NFM
    cout<<"\t-Q\t\t\tUse quadri correlator for FM demodulation (default is atan2)\n";
#endif
    cout<<"\t-c <config_file_path>\tUse non-default configuration file\n\t\t\t\t(default: "<<CFGFILE<<")\n\
\t-v\t\t\tDisplay version and exit\n";
    exit(EXIT_SUCCESS);
} 

int main(int argc, char* argv[]) {
#pragma GCC diagnostic ignored "-Wwrite-strings"
    char *cfgfile = CFGFILE;
#ifndef _WIN32
    char *pidfile = PIDFILE;
#endif
#pragma GCC diagnostic warning "-Wwrite-strings"
    int opt;

#ifdef NFM
    while((opt = getopt(argc, argv, "Qfhvc:")) != -1) {
#else
    while((opt = getopt(argc, argv, "fhvc:")) != -1) {
#endif
        switch(opt) {
#ifdef NFM
            case 'Q':
                fm_demod = FM_QUADRI_DEMOD;
                break;
#endif
            case 'f':
                foreground = 1;
                break;
            case 'c':
                cfgfile = optarg;
                break;
            case 'v':
                cout<<"RTLSDR-Airband version "<<RTL_AIRBAND_VERSION<<"\n";
                exit(EXIT_SUCCESS);
            case 'h':
            default:
                usage();
                break;
       }
    }

#ifndef __arm__
#ifdef _WIN32
// check cpu features
	int cpuinfo[4];
	__cpuid(cpuinfo, 1);
	if (cpuinfo[2] & 1 << 28) {
		avx = 1;
	} else if (cpuinfo[2] & 1) {
		avx = 0;
#else /* !_WIN32 */
#define cpuid(func,ax,bx,cx,dx)\
	__asm__ __volatile__ ("cpuid":\
        "=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));
	int a,b,c,d;
	cpuid(1,a,b,c,d);
	if((int)((d >> 25) & 0x1)) {
		/* NOOP */
#endif /* !_WIN32 */
	} else {
		printf("Unsupported CPU.\n");
		error();
	}
#endif /* !__arm__ */

    // read config
    try {
        Config config;
        config.readFile(cfgfile);
        Setting &root = config.getRoot();
        if(root.exists("syslog")) do_syslog = root["syslog"];
#ifndef _WIN32
        if(root.exists("pidfile")) pidfile = strdup(root["pidfile"]);
#endif
        if(root.exists("rtlsdr_buffers")) rtlsdr_buffers = (int)(root["rtlsdr_buffers"]);
        if(rtlsdr_buffers < 1) {
            cerr<<"Configuration error: rtlsdr_buffers must be greater than 0\n";
            error();
        }
        if(root.exists("shout_metadata_delay")) shout_metadata_delay = (int)(root["shout_metadata_delay"]);
        if(shout_metadata_delay < 0 || shout_metadata_delay > 2*TAG_QUEUE_LEN) {
            cerr<<"Configuration error: shout_metadata_delay is out of allowed range (0-"<<2 * TAG_QUEUE_LEN<<")\n";
            error();
        }
        if(root.exists("localtime") && (bool)root["localtime"] == true)
            use_localtime = true;
#ifdef NFM
        if(root.exists("tau"))
            alpha = ((int)root["tau"] == 0 ? 0.0f : exp(-1.0f/(WAVE_RATE * 1e-6 * (int)root["tau"])));
#endif
        Setting &devs = config.lookup("devices");
        device_count = devs.getLength();
        if (device_count < 1) {
            cerr<<"Configuration error: no devices defined\n";
            error();
        }
#ifndef _WIN32
        struct sigaction sigact, pipeact;

        memset(&sigact, 0, sizeof(sigact));
        memset(&pipeact, 0, sizeof(pipeact));
        pipeact.sa_handler = SIG_IGN;
        sigact.sa_handler = &sighandler;
        sigaction(SIGPIPE, &pipeact, NULL);
        sigaction(SIGHUP, &sigact, NULL);
        sigaction(SIGINT, &sigact, NULL);
        sigaction(SIGQUIT, &sigact, NULL);
        sigaction(SIGTERM, &sigact, NULL);
#else
        SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

        uintptr_t tempptr = (uintptr_t)malloc(device_count * sizeof(device_t)+31);
        tempptr &= ~0x0F;
        devices = (device_t *)tempptr;
        shout_init();
#ifndef _WIN32
        if(do_syslog) openlog("rtl_airband", LOG_PID, LOG_DAEMON);
#endif
        int ii = 0;
        for (int i = 0; i < devs.getLength(); i++) {
            if(devs[i].exists("disable") && (bool)devs[i]["disable"] == true) continue;
            device_t* dev = devices + ii;
            if(!devs[i].exists("correction")) devs[i].add("correction", Setting::TypeInt);
            dev->device = (int)devs[i]["index"];
            dev->channel_count = 0;
            if(devs[i].exists("gain"))
                dev->gain = (int)devs[i]["gain"] * 10;
            else
                dev->gain = AUTO_GAIN;
            if(devs[i].exists("mode")) {
                if(!strncmp(devs[i]["mode"], "multichannel", 12)) {
                    dev->mode = R_MULTICHANNEL;
                } else if(!strncmp(devs[i]["mode"], "scan", 4)) {
                    dev->mode = R_SCAN;
                } else {
                    cerr<<"Configuration error: devices.["<<i<<"]: invalid mode (must be one of: \"scan\", \"multichannel\")\n";
                    error();
                }
            } else {
                dev->mode = R_MULTICHANNEL;
            }
            if(dev->mode == R_MULTICHANNEL) dev->centerfreq = (int)devs[i]["centerfreq"];
#ifdef NFM
            if(devs[i].exists("tau")) {
                dev->alpha = ((int)devs[i]["tau"] == 0 ? 0.0f : exp(-1.0f/(WAVE_RATE * 1e-6 * (int)devs[i]["tau"])));
            } else {
                dev->alpha = alpha;
            }
#endif
            dev->correction = (int)devs[i]["correction"];
            memset(dev->bins, 0, sizeof(dev->bins));
            memset(dev->base_bins, 0, sizeof(dev->base_bins));
            dev->bufs = dev->bufe = dev->waveend = dev->waveavail = dev->row = dev->tq_head = dev->tq_tail = 0;
            dev->last_frequency = -1;
            int jj = 0;
            for (int j = 0; j < devs[i]["channels"].getLength(); j++)  {
                if(devs[i]["channels"][j].exists("disable") && (bool)devs[i]["channels"][j]["disable"] == true) {
                    continue;
                }
                channel_t* channel = dev->channels + jj;
                for (int k = 0; k < AGC_EXTRA; k++) {
                    channel->wavein[k] = 20;
                    channel->waveout[k] = 0.5;
                }
                channel->agcsq = 1;
                channel->axcindicate = ' ';
                channel->agcavgfast = 0.5f;
                channel->agcavgslow = 0.5f;
                channel->agcmin = 100.0f;
                channel->agclow = 0;
                channel->sqlevel = -1.0f;
                channel->modulation = MOD_AM;
                if(devs[i]["channels"][j].exists("modulation")) {
#ifdef NFM
                    if(!strncmp(devs[i]["channels"][j]["modulation"], "nfm", 3)) {
                        channel->modulation = MOD_NFM;
                    } else 
#endif
                    if(!strncmp(devs[i]["channels"][j]["modulation"], "am", 2)) {
                        channel->modulation = MOD_AM;
                    } else {
                        cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: unknown modulation\n";
                        error();
                    }
                }
                if(devs[i]["channels"][j].exists("squelch")) {
                    channel->sqlevel = (int)devs[i]["channels"][j]["squelch"];
                    if(channel->sqlevel <= 0) {
                        cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: squelch must be greater than 0\n";
                        error();
                    }
                }
                channel->afc = devs[i]["channels"][j].exists("afc") ? (unsigned char) (unsigned int)devs[i]["channels"][j]["afc"] : 0;
                if(dev->mode == R_MULTICHANNEL) {
                    channel->frequency = devs[i]["channels"][j]["freq"];
                } else { /* R_SCAN */
                    channel->freq_count = devs[i]["channels"][j]["freqs"].getLength();
                    if(channel->freq_count < 1) {
                        cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: freqs should be a list with at least one element\n";
                        error();
                    }
                    if(devs[i]["channels"][j].exists("labels") && devs[i]["channels"][j]["labels"].getLength() < channel->freq_count) {
                        cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: labels should be a list with at least "
                            <<channel->freq_count<<" elements\n";
                        error();
                    }
                    channel->freqlist = (int *)malloc(channel->freq_count * sizeof(int));
                    channel->labels = (char **)malloc(channel->freq_count * sizeof(char *));
                    memset(channel->labels, 0, channel->freq_count * sizeof(char *));
                    if(channel->freqlist == NULL || channel->labels == NULL) {
                        cerr<<"Cannot allocate memory for freqlist\n";
                        error();
                    }
                    for(int f = 0; f<channel->freq_count; f++) {
                        channel->freqlist[f] = (int)(devs[i]["channels"][j]["freqs"][f]);
                        if(devs[i]["channels"][j].exists("labels"))
                            channel->labels[f] = strdup(devs[i]["channels"][j]["labels"][f]);
                    }
// Set initial frequency for scanning
// We tune 2 FFT bins higher to avoid DC spike
                    channel->frequency = channel->freqlist[0];
                    dev->centerfreq = channel->freqlist[0] + 2 * (double)(SOURCE_RATE / FFT_SIZE);
                }
#ifdef NFM
                if(devs[i]["channels"][j].exists("tau")) {
                    channel->alpha = ((int)devs[i]["channels"][j]["tau"] == 0 ? 0.0f : exp(-1.0f/(WAVE_RATE * 1e-6 * (int)devs[i]["channels"][j]["tau"])));
                } else {
                    channel->alpha = dev->alpha;
                }
#endif
                channel->output_count = devs[i]["channels"][j]["outputs"].getLength();
                if(channel->output_count < 1) {
                    cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: no outputs defined\n";
                    error();
                }
                channel->outputs = (output_t *)malloc(channel->output_count * sizeof(struct output_t));
                if(channel->outputs == NULL) {
                    cerr<<"Cannot allocate memory for outputs\n";
                    error();
                }
                int oo = 0;
                for(int o = 0; o < channel->output_count; o++) {
                    if(devs[i]["channels"][j]["outputs"][o].exists("disable") && (bool)devs[i]["channels"][j]["outputs"][o]["disable"] == true) {
                        continue;
                    }
                    if(!strncmp(devs[i]["channels"][j]["outputs"][o]["type"], "icecast", 7)) {
                        channel->outputs[oo].data = malloc(sizeof(struct icecast_data));
                        if(channel->outputs[oo].data == NULL) {
                            cerr<<"Cannot allocate memory for outputs\n";
                            error();
                        }
                        memset(channel->outputs[oo].data, 0, sizeof(struct icecast_data));
                        channel->outputs[oo].type = O_ICECAST;
                        icecast_data *idata = (icecast_data *)(channel->outputs[oo].data);
                        idata->hostname = strdup(devs[i]["channels"][j]["outputs"][o]["server"]);
                        idata->port = devs[i]["channels"][j]["outputs"][o]["port"];
                        idata->mountpoint = strdup(devs[i]["channels"][j]["outputs"][o]["mountpoint"]);
                        idata->username = strdup(devs[i]["channels"][j]["outputs"][o]["username"]);
                        idata->password = strdup(devs[i]["channels"][j]["outputs"][o]["password"]);
                        if(devs[i]["channels"][j]["outputs"][o].exists("name"))
                            idata->name = strdup(devs[i]["channels"][j]["outputs"][o]["name"]);
                        if(devs[i]["channels"][j]["outputs"][o].exists("genre"))
                            idata->genre = strdup(devs[i]["channels"][j]["outputs"][o]["genre"]);
                        if(devs[i]["channels"][j]["outputs"][o].exists("send_scan_freq_tags"))
                            idata->send_scan_freq_tags = (bool)devs[i]["channels"][j]["outputs"][o]["send_scan_freq_tags"];
                        else
                            idata->send_scan_freq_tags = 0;
                    } else if(!strncmp(devs[i]["channels"][j]["outputs"][o]["type"], "file", 4)) {
                        channel->outputs[oo].data = malloc(sizeof(struct file_data));
                        if(channel->outputs[oo].data == NULL) {
                            cerr<<"Cannot allocate memory for outputs\n";
                            error();
                        }
                        memset(channel->outputs[oo].data, 0, sizeof(struct file_data));
                        channel->outputs[oo].type = O_FILE;
                        file_data *fdata = (file_data *)(channel->outputs[oo].data);
                        fdata->dir = strdup(devs[i]["channels"][j]["outputs"][o]["directory"]);
                        fdata->prefix = strdup(devs[i]["channels"][j]["outputs"][o]["filename_template"]);
                        fdata->continuous = devs[i]["channels"][j]["outputs"][o].exists("continuous") ?
                            (bool)(devs[i]["channels"][j]["outputs"][o]["continuous"]) : false;
                        fdata->append = (!devs[i]["channels"][j]["outputs"][o].exists("append")) || (bool)(devs[i]["channels"][j]["outputs"][o]["append"]);
                    } else {
                        cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"] outputs["<<o<<"]: unknown output type\n";
                        error();
                    }
                    channel->outputs[oo].enabled = true;
                    channel->outputs[oo].active = false;
                    oo++;
                }
                if(oo < 1) {
                    cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: no outputs defined\n";
                    error();
                }
                channel->outputs = (output_t *)realloc(channel->outputs, oo * sizeof(struct output_t));
                if(channel->outputs == NULL) {
                    cerr<<"Cannot allocate memory for outputs\n";
                    error();
                }
                channel->output_count = oo;

                dev->base_bins[jj] = dev->bins[jj] = (int)ceil((channel->frequency + SOURCE_RATE - dev->centerfreq) / (double)(SOURCE_RATE / FFT_SIZE) - 1.0f) % FFT_SIZE;
#ifdef NFM
                if(channel->modulation == MOD_NFM) {
// Calculate mixing frequency needed for NFM to remove linear phase shift caused by FFT sliding window
// This equals bin_width_Hz * (distance_from_DC_bin)
                    float timeref_freq = 2.0f * M_PI * (float)(SOURCE_RATE / FFT_SIZE) *
                      (float)(dev->bins[jj] < (FFT_SIZE >> 1) ? dev->bins[jj] + 1 : dev->bins[jj] - FFT_SIZE + 1) / (float)WAVE_RATE;
// Pre-generate the waveform for better performance
                    for(int k = 0; k < WAVE_RATE; k++) {
                        channel->timeref_cos[k] = cosf(timeref_freq * k);
                        channel->timeref_nsin[k] = -sinf(timeref_freq * k);
                    }
                }
#endif
                jj++;
            }
            if(jj < 1 || jj > 8) {
                cerr<<"Configuration error: devices.["<<i<<"]: invalid channel count (min 1, max 8)\n";
                error();
            }
            if(dev->mode == R_SCAN && jj > 1) {
                cerr<<"Configuration error: devices.["<<i<<"]: only one channel section is allowed in scan mode\n";
                error();
            }
            dev->channel_count = jj;
            ii++;
        }
        if (ii < 1) {
            cerr<<"Configuration error: no devices defined\n";
            error();
        }
        int device_count2 = rtlsdr_get_device_count();
        if (device_count2 < ii) {
            cerr<<"Not enough devices ("<<ii<<" configured, "<<device_count2<<" detected)\n";
            error();
        }
        for(int i = 0; i < ii; i++) {
            device_t* dev = devices + i;
            if(dev->device >= device_count2) {
                cerr<<"Specified device id "<<(int)dev->device<<" is greater or equal than number of devices ("<<device_count2<<")\n";
                error();
            }
        }
        device_count = ii;
    } catch(FileIOException e) {
            cerr<<"Cannot read configuration file "<<cfgfile<<"\n";
            error();
    } catch(ParseException e) {
            cerr<<"Error while parsing configuration file "<<cfgfile<<" line "<<e.getLine()<<": "<<e.getError()<<"\n";
            error();
    } catch(SettingNotFoundException e) {
            cerr<<"Configuration error: mandatory parameter missing: "<<e.getPath()<<"\n";
            error();
    } catch(SettingTypeException e) {
            cerr<<"Configuration error: invalid parameter type: "<<e.getPath()<<"\n";
            error();
    } catch(ConfigException e) {
            cerr<<"Unhandled config exception\n";
            error();
    }
#ifndef _WIN32
    if(!foreground) {
        int pid1, pid2;
        if((pid1 = fork()) == -1) {
            cerr<<"Cannot fork child process: "<<strerror(errno)<<"\n";
            error();
        }
        if(pid1) {
            waitpid(-1, NULL, 0);
            return(0);
        } else {
            if((pid2 = fork()) == -1) {
                cerr<<"Cannot fork child process: "<<strerror(errno)<<"\n";
                error();
            }
            if(pid2) {
                return(0);
            } else {
                int nullfd, dupfd;
                if((nullfd = open("/dev/null", O_RDWR)) == -1) {
                    log(LOG_CRIT, "Cannot open /dev/null: %s\n", strerror(errno));
                    error();
                }
                for(dupfd = 0; dupfd <= 2; dupfd++) {
                    if(dup2(nullfd, dupfd) == -1) {
                        log(LOG_CRIT, "dup2(): %s\n", strerror(errno));
                        error();
                    }
                }
                if(nullfd > 2) close(nullfd);
                FILE *f = fopen(pidfile, "w");
                if(f == NULL) {
                    log(LOG_CRIT, "Cannot write pidfile: %s\n", strerror(errno));
                    error();
                } else {
                    fprintf(f, "%ld\n", (long)getpid());
                    fclose(f);
                }
            }
        }
    }
#endif
    log(LOG_INFO, "RTLSDR-Airband version %s starting\n", RTL_AIRBAND_VERSION);
    for (int i = 0; i < device_count; i++) {
        device_t* dev = devices + i;
        for (int j = 0; j < dev->channel_count; j++)  {
            channel_t* channel = dev->channels + j;
            channel->lame = airlame_init();
            for (int k = 0; k < channel->output_count; k++) {
                output_t *output = channel->outputs + k;
                if(output->type == O_ICECAST)
                    shout_setup((icecast_data *)(output->data));
            }
        }
#ifdef _WIN32
        dev->rtl_thread = (THREAD)_beginthread(rtlsdr_exec, 0, dev);
        if(dev->mode == R_SCAN)
            dev->controller_thread = (THREAD)_beginthread(controller_thread, 0, dev);
#else
        pthread_create(&dev->rtl_thread, NULL, &rtlsdr_exec, dev);
        if(dev->mode == R_SCAN) {
            if(pthread_mutex_init(&dev->tag_queue_lock, NULL) != 0) {
                cerr<<"Failed to initialize mutex - aborting\n";
                error();
            }
// FIXME: not needed when freq_count == 1?
            pthread_create(&dev->controller_thread, NULL, &controller_thread, dev);
        }
#endif
    }

    int timeout = 50;   // 5 seconds
    while (atomic_get(&device_opened) != device_count && timeout > 0) {
        SLEEP(100);
        timeout--;
    }
    if(atomic_get(&device_opened) != device_count) {
        cerr<<"Some devices failed to initialize - aborting\n";
        error();
    }
    if (foreground) {
#ifdef _WIN32
        system("cls");
#else
        printf("\e[1;1H\e[2J");
#endif

        GOTOXY(0, 0);
        printf("                                                                               ");
        for (int i = 0; i < device_count; i++) {
            GOTOXY(0, i * 17 + 1);
            for (int j = 0; j < devices[i].channel_count; j++) {
                printf(" %7.3f  ", devices[i].channels[j].frequency / 1000000.0);
            }
            if (i != device_count - 1) {
                GOTOXY(0, i * 17 + 16);
                printf("-------------------------------------------------------------------------------");
            }
        }
    }
    THREAD thread2;
#ifdef _WIN32
    thread2 = (THREAD)_beginthread(icecast_check, 0, NULL);
#else
    pthread_create(&thread2, NULL, &icecast_check, NULL);
    THREAD thread3;
    pthread_create(&thread3, NULL, &output_thread, NULL);
#endif

    demodulate();

    log(LOG_INFO, "cleaning up\n");
    for (int i = 0; i < device_count; i++) {
        rtlsdr_cancel_async(devices[i].rtlsdr);
        pthread_join(devices[i].rtl_thread, NULL);
        if(devices[i].mode == R_SCAN)
            pthread_join(devices[i].controller_thread, NULL);
    }
    log(LOG_INFO, "rtlsdr threads closed\n");
#ifndef _WIN32
    if(!foreground) unlink(pidfile);
#endif
    return 0;
}
// vim: ts=4:expandtab
