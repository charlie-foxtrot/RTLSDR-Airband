/*
 * RTLSDR AM demodulator and streaming
 * 
 * Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
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

#ifdef _WIN32

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
#else /* !_WIN32 */
#if defined __arm__
#include "hello_fft/mailbox.h"
#include "hello_fft/gpu_fft.h"
#else
#include <xmmintrin.h>
#include <fftw3.h>
#endif /* !__arm__ */
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
#include <fcntl.h>
#include <sys/wait.h>
#include <algorithm>
#include <csignal>
#include <cstdarg>
#include <cstring>
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
#define WAVE_RATE 8000
#define WAVE_BATCH 1000
#define WAVE_LEN 2048
#define MP3_RATE 8000
#define MAX_SHOUT_QUEUELEN 32768
#define AGC_EXTRA 48
#define FFT_SIZE 512
#define CHANNELS 8

#if defined __arm__
extern "C" void samplefft(GPU_FFT_COMPLEX* dest, unsigned char* buffer, float* window, float* levels);
extern "C" void fftwave(float* dest, GPU_FFT_COMPLEX* src, int* sizes, int* bins);
#define FFT_SIZE_LOG 9
#define FFT_BATCH 250
#else
#define FFT_BATCH 1
#endif
#if defined _WIN32
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")
#endif /* _WIN32 */

using namespace std;
using namespace libconfig;

struct channel_t {
    float wavein[WAVE_LEN];  // FFT output waveform
    float waveref[WAVE_LEN]; // for power level calculation
    float waveout[WAVE_LEN]; // waveform after squelch + AGC
    int agcsq;             // squelch status, 0 = signal, 1 = suppressed
    char agcindicate;  // squelch status indicator
    float agcavgfast;  // average power, for AGC
    float agcavgslow;  // average power, for squelch level detection
    float agcmin;      // noise level
    int agclow;             // low level sample count
    int frequency;
    int freq_count;
    int *freqlist;
    const char *hostname;
    int port;
    const char *username;
    const char *password;
    const char *mountpoint;
    const char *name;
    const char *genre;
    shout_t * shout;
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
    int channel_count;
    int bins[8];
    channel_t channels[8];
    int waveend;
    int waveavail;
    THREAD rtl_thread;
    THREAD controller_thread;
    int row;
    int failed;
    enum rec_modes mode;
};

device_t* devices;
int device_count;
int device_opened = 0;
#ifdef _WIN32
int avx;
#endif
int foreground = 0, do_syslog = 0;
static volatile int do_exit = 0;

void error() {
#ifdef _WIN32
    system("pause");
#endif
    exit(1);
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
    device_t *dev = (device_t*)params;
    rtlsdr_open(&dev->rtlsdr, dev->device);
    if (NULL == dev) {
        log(LOG_ERR, "Failed to open rtlsdr device #%d.\n", dev->device);
        error();
        return NULL;
    }
    rtlsdr_set_sample_rate(dev->rtlsdr, SOURCE_RATE);
    rtlsdr_set_center_freq(dev->rtlsdr, dev->centerfreq);
    rtlsdr_set_freq_correction(dev->rtlsdr, dev->correction);
    if(dev->gain == AUTO_GAIN) {
        rtlsdr_set_tuner_gain_mode(dev->rtlsdr, 0);
        log(LOG_INFO, "Device #%d: gain set to automatic", dev->device);
    } else {
        rtlsdr_set_tuner_gain_mode(dev->rtlsdr, 1);
        rtlsdr_set_tuner_gain(dev->rtlsdr, dev->gain);
        log(LOG_INFO, "Device #%d: gain set to %0.2f dB", dev->device, (float)rtlsdr_get_tuner_gain(dev->rtlsdr) / 10.0);
    }
    rtlsdr_set_agc_mode(dev->rtlsdr, 0);
    rtlsdr_reset_buffer(dev->rtlsdr);
    log(LOG_INFO, "Device %d started.\n", dev->device);
    device_opened++;
    dev->failed = 0;
    if(rtlsdr_read_async(dev->rtlsdr, rtlsdr_callback, params, 20, 320000) < 0) {
        log(LOG_WARNING, "Device #%d: async read failed, disabling", dev->device);
        dev->failed = 1;
        device_opened--;
    }
    return 0;
}

void mp3_setup(channel_t* channel) {
    int ret;
    shout_t * shouttemp = shout_new();
    if (shouttemp == NULL) {
        printf("cannot allocate\n");
    }
    if (shout_set_host(shouttemp, channel->hostname) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_protocol(shouttemp, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_port(shouttemp, channel->port) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    char mp[100];
#ifdef _WIN32
    sprintf_s(mp, 80, "/%s", channel->mountpoint);
#else
    sprintf(mp, "/%s", channel->mountpoint);
#endif
    if (shout_set_mount(shouttemp, mp) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_user(shouttemp, channel->username) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_password(shouttemp, channel->password) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if (shout_set_format(shouttemp, SHOUT_FORMAT_MP3) != SHOUTERR_SUCCESS){
        shout_free(shouttemp); return;
    }
    if(channel->name && shout_set_name(shouttemp, channel->name) != SHOUTERR_SUCCESS) {
        shout_free(shouttemp); return;
    }
    if(channel->genre && shout_set_genre(shouttemp, channel->genre) != SHOUTERR_SUCCESS) {
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
            channel->hostname, channel->port, channel->mountpoint);

    while (ret == SHOUTERR_BUSY) {
        usleep(10000);
        ret = shout_get_connected(shouttemp);
    }
 
    if (ret == SHOUTERR_CONNECTED) {
        log(LOG_NOTICE, "Connected to %s:%d/%s\n", 
            channel->hostname, channel->port, channel->mountpoint);
        channel->lame = lame_init();
        lame_set_in_samplerate(channel->lame, WAVE_RATE);
        lame_set_VBR(channel->lame, vbr_off);
        lame_set_brate(channel->lame, 16);
        lame_set_quality(channel->lame, 7);
        lame_set_out_samplerate(channel->lame, MP3_RATE);
        lame_set_num_channels(channel->lame, 1);
        lame_set_mode(channel->lame, MONO);
        lame_init_params(channel->lame);
        SLEEP(100);
        channel->shout = shouttemp;
    } else {
        log(LOG_WARNING, "Could not connect to %s:%d/%s\n",
            channel->hostname, channel->port, channel->mountpoint);
        shout_free(shouttemp);
        return;
    }
}

unsigned char lamebuf[22000];
void mp3_process(channel_t* channel) {
    if (channel->shout == NULL) {
        return;
    }
    int bytes = lame_encode_buffer_ieee_float(channel->lame, channel->waveout, NULL, WAVE_BATCH, lamebuf, 22000);
    if (bytes > 0) {
        int ret = shout_send(channel->shout, lamebuf, bytes);
        if (ret != SHOUTERR_SUCCESS || shout_queuelen(channel->shout) > MAX_SHOUT_QUEUELEN) {
            if (shout_queuelen(channel->shout) > MAX_SHOUT_QUEUELEN)
                log(LOG_WARNING, "Exceeded max backlog for %s:%d/%s, disconnecting\n",
                    channel->hostname, channel->port, channel->mountpoint);
            // reset connection
            log(LOG_WARNING, "Lost connection to %s:%d/%s\n",
                channel->hostname, channel->port, channel->mountpoint);
            shout_close(channel->shout);
            shout_free(channel->shout);
            channel->shout = NULL;
            lame_close(channel->lame);
            channel->lame = NULL;
            return;
        }
    }
}

#ifndef _WIN32
pthread_cond_t      mp3_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mp3_mutex = PTHREAD_MUTEX_INITIALIZER;
void* mp3_thread(void* params) {
    while (!do_exit) {
        pthread_cond_wait(&mp3_cond, &mp3_mutex);
        for (int i = 0; i < device_count; i++) {
            if (!devices[i].failed && devices[i].waveavail) {
                devices[i].waveavail = 0;
                for (int j = 0; j < devices[i].channel_count; j++) {
                    channel_t* channel = devices[i].channels + j;
                    mp3_process(channel);
                    memcpy(channel->waveout, channel->waveout + WAVE_BATCH, AGC_EXTRA * 4);
                }
            }
        }
    }
    return 0;
}
#endif

void* controller_thread(void* params) {
    device_t *dev = (device_t*)params;
    int i = 1;
    int consecutive_squelch_off = 0;
    if(dev->channels[0].freq_count < 2) return 0;
    while(!do_exit) {
        SLEEP(200);
        if(dev->channels[0].agcindicate == ' ') {
            if(consecutive_squelch_off < 10) {
                consecutive_squelch_off++;
            } else {
		dev->channels[0].frequency = dev->channels[0].freqlist[i];
                dev->centerfreq = dev->channels[0].freqlist[i] + 2 * (double)(SOURCE_RATE / FFT_SIZE);
                rtlsdr_set_center_freq(dev->rtlsdr, dev->centerfreq);
                i++; i %= dev->channels[0].freq_count;
            }
        } else {
            consecutive_squelch_off = 0;
        }
    }
    return 0;
}

// reconnect as required
#ifdef _WIN32
void mp3_check(void* params) {
#else
void* mp3_check(void* params) {
#endif
    while (true) {
        SLEEP(10000);
        for (int i = 0; i < device_count; i++) {
            device_t* dev = devices + i;
            for (int j = 0; j < dev->channel_count; j++) {
                if(dev->failed) {
                    if(dev->channels[j].shout) {
                        log(LOG_WARNING, "Device #%d failed, disconnecting stream %s:%d/%s\n",
                            i, dev->channels[j].hostname, dev->channels[j].port, dev->channels[j].mountpoint);
                        shout_close(dev->channels[j].shout);
                        shout_free(dev->channels[j].shout);
                        dev->channels[j].shout = NULL;
                    }
                    if(dev->channels[j].lame) {
                        lame_close(dev->channels[j].lame);
                        dev->channels[j].lame = NULL;
                    }
                } else {
                    if (dev->channels[j].shout == NULL){
                        log(LOG_NOTICE, "Trying to reconnect to %s:%d/%s...\n",
                            dev->channels[j].hostname, dev->channels[j].port, dev->channels[j].mountpoint);
                        mp3_setup(dev->channels + j);
                    }
                }
            }
        }
    }
#ifndef _WIN32
    return 0;
#endif
}

void demodulate() {

    // initialize fft engine
#ifndef __arm__
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
    int sizes[2];
    sizes[0] = fft->step * sizeof(GPU_FFT_COMPLEX);
    sizes[1] = sizeof(channel_t);
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
#ifdef __arm__
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

        if(!device_opened) {
            log(LOG_ERR, "All receivers failed, exiting");
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

#ifdef __arm__
        for (int i = 0; i < FFT_BATCH; i++) {
            samplefft(fft->in + i * fft->step, dev->buffer + dev->bufs + i * speed2, window, levels);
        }

        // allow mp3 encoding thread to run while waiting for GPU to finish
        pthread_cond_signal(&mp3_cond);

        gpu_fft_execute(fft);

        fftwave(dev->channels[0].wavein + dev->waveend, fft->out, sizes, dev->bins);
#else
        // process 4 rtl samples (16 bytes)
#ifdef _WIN32
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
#endif /* _WIN32 */
            for (int i = 0; i < FFT_SIZE; i += 2) {
                unsigned char* buf2 = dev->buffer + dev->bufs + i * 2;
                __m128 a = _mm_set_ps(levels[*(buf2 + 3)], levels[*(buf2 + 2)], levels[*(buf2 + 1)], levels[*(buf2)]);
                __m128 b = _mm_load_ps(&window[i * 2]);
                a = _mm_mul_ps(a, b);
                _mm_store_ps(&fftin[i][0], a);
            }
#ifdef _WIN32
        }
#else
        // allow mp3 encoding thread to run while waiting for FFT to finish
        pthread_cond_signal(&mp3_cond);
#endif

        fftwf_execute(fft);

	for (int j = 0; j < dev->channel_count; j++) {
		dev->channels[j].wavein[dev->waveend] = 
		  sqrtf(fftout[dev->bins[j]][0] * fftout[dev->bins[j]][0] + fftout[dev->bins[j]][1] * fftout[dev->bins[j]][1]);
	}
#endif /* !__arm__ */

        dev->waveend += FFT_BATCH;
        
        if (dev->waveend >= WAVE_BATCH + AGC_EXTRA) {
            if (foreground) {
                GOTOXY(0, device_num * 17 + dev->row + 3);
            }
            for (int i = 0; i < dev->channel_count; i++) {
                channel_t* channel = dev->channels + i;
#ifdef __arm__
                float agcmin2 = channel->agcmin * 4.5f;
                for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j++) {
                    channel->waveref[j] = min(channel->wavein[j], agcmin2);
                }
#else /* !__arm__ */
#ifdef _WIN32
                if (avx) {
                    __m256 agccap = _mm256_set1_ps(channel->agcmin * 4.5f);
                    for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 8) {
                        __m256 t = _mm256_loadu_ps(channel->wavein + j);
                        _mm256_storeu_ps(channel->waveref + j, _mm256_min_ps(t, agccap));
                    }
                } else {
#endif /* _WIN32 */
                    __m128 agccap = _mm_set1_ps(channel->agcmin * 4.5f);
                    for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 4) {
                        __m128 t = _mm_loadu_ps(channel->wavein + j);
                        _mm_storeu_ps(channel->waveref + j, _mm_min_ps(t, agccap));
                    }
#ifdef _WIN32
                }
#endif /* _WIN32 */
#endif /* !__arm__ */
                for (int j = AGC_EXTRA; j < WAVE_BATCH + AGC_EXTRA; j++) {
                    // auto noise floor
                    if (j % 16 == 0) {
                        channel->agcmin = channel->agcmin * 0.97f + min(channel->agcavgslow, channel->agcmin) * 0.03f + 0.0001f;
                    }

                    // average power
                    channel->agcavgslow = channel->agcavgslow * 0.99f + channel->waveref[j] * 0.01f;

                    if (channel->agcsq > 0) {
                        channel->agcsq = max(channel->agcsq - 1, 1);
                        if (channel->agcsq == 1 && channel->agcavgslow > 3.0f * channel->agcmin) {
                            channel->agcsq = -AGC_EXTRA * 2;
                            channel->agcindicate = '*';
                            // fade in
                            for (int k = j - AGC_EXTRA; k < j; k++) {
                                if (channel->wavein[k] > channel->agcmin * 3.0f) {
                                    channel->agcavgfast = channel->agcavgfast * 0.98f + channel->wavein[k] * 0.02f;
                                }
                            }
                        }
                    } else {
                        if (channel->wavein[j] > channel->agcmin * 3.0f) {
                            channel->agcavgfast = channel->agcavgfast * 0.995f + channel->wavein[j] * 0.005f;
                            channel->agclow = 0;
                        } else {
                            channel->agclow++;
                        }
                        channel->agcsq = min(channel->agcsq + 1, -1);
                        if ((channel->agcsq == -1 && channel->agcavgslow < 2.4f * channel->agcmin) || channel->agclow == AGC_EXTRA - 12) {
                            channel->agcsq = AGC_EXTRA * 2;
                            channel->agcindicate = ' ';
                            // fade out
                            for (int k = j - AGC_EXTRA + 1; k < j; k++) {
                                channel->waveout[k] = channel->waveout[k - 1] * 0.94f;
                            }
                        }
                    }
                    channel->waveout[j] = (channel->agcsq != -1) ? 0 : (channel->wavein[j - AGC_EXTRA] - channel->agcavgfast) / (channel->agcavgfast * 2.5f);
                    if (abs(channel->waveout[j]) > 0.8f) {
                        channel->waveout[j] *= 0.85f;
                        channel->agcavgfast *= 1.15f;
                    }
                }
#ifdef _WIN32
                mp3_process(channel);
                memcpy(channel->waveout, channel->waveout + WAVE_BATCH, AGC_EXTRA * 4);
#endif
                memcpy(channel->wavein, channel->wavein + WAVE_BATCH, (dev->waveend - WAVE_BATCH) * 4);
                if (foreground) {
                    if(dev->mode == R_SCAN)
                        printf("%4.0f/%3.0f%c %7.3f", channel->agcavgslow, channel->agcmin, channel->shout == NULL ? 'X' : channel->agcindicate, (dev->channels[0].frequency / 1000000.0));
                    else
                        printf("%4.0f/%3.0f%c", channel->agcavgslow, channel->agcmin, channel->shout == NULL ? 'X' : channel->agcindicate);
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
    cout<<"Usage: rtl_airband [-f] [-c <config_file_path>]\n\
\t-h\t\t\tDisplay this help text\n\
\t-f\t\t\tRun in foreground, display textual waterfalls\n\
\t-c <config_file_path>\tUse non-default configuration file\n\t\t\t\t(default: "<<CFGFILE<<")\n";
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

    while((opt = getopt(argc, argv, "fhc:")) != -1) {
        switch(opt) {
            case 'f':
                foreground = 1;
                break;
            case 'c':
                cfgfile = optarg;
                break;
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
        Setting &devs = config.lookup("devices");
        device_count = devs.getLength();
        if (device_count < 1) {
            cerr<<"Configuration error: no devices defined\n";
            error();
        }
        int device_count2 = rtlsdr_get_device_count();
        if (device_count2 < device_count) {
            cerr<<"Not enough devices ("<<device_count<<" configured, "<<device_count2<<" detected)\n";
            error();
        }
#ifndef _WIN32
        struct sigaction sigact, pipeact;

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
        for (int i = 0; i < devs.getLength(); i++) {
            device_t* dev = devices + i;
            if((int)devs[i]["index"] >= device_count2) {
                cerr<<"Specified device id "<<(int)devs[i]["index"]<<" is >= number of devices "<<device_count2<<"\n";
                error();
            }
            if(!devs[i].exists("correction")) devs[i].add("correction", Setting::TypeInt);
            dev->device = (int)devs[i]["index"];
            dev->channel_count = devs[i]["channels"].getLength();
            if(dev->channel_count < 1 || dev->channel_count > 8) {
                cerr<<"Configuration error: devices.["<<i<<"]: invalid channel count (min 1, max 8)\n";
                error();
            }
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
            if(dev->mode == R_SCAN && dev->channel_count > 1) {
                cerr<<"Configuration error: devices.["<<i<<"]: only one channel section is allowed in scan mode\n";
                error();
            }
            dev->correction = (int)devs[i]["correction"];
            dev->bins[0] = dev->bins[1] = dev->bins[2] = dev->bins[3] = dev->bins[4] = dev->bins[5] = dev->bins[6] = dev->bins[7] = 0;
            dev->bufs = dev->bufe = dev->waveend = dev->waveavail = dev->row = 0;
            for (int j = 0; j < dev->channel_count; j++)  {
                channel_t* channel = dev->channels + j;
                for (int k = 0; k < AGC_EXTRA; k++) {
                    channel->wavein[k] = 20;
                    channel->waveout[k] = 0.5;
                }
                channel->agcsq = 1;
                channel->agcindicate = ' ';
                channel->agcavgfast = 0.5f;
                channel->agcavgslow = 0.5f;
                channel->agcmin = 100.0f;
                channel->agclow = 0;
                channel->hostname = strdup(devs[i]["channels"][j]["server"]);
// FIXME: default port number
                channel->port = devs[i]["channels"][j]["port"];
                channel->mountpoint = strdup(devs[i]["channels"][j]["mountpoint"]);
                if(dev->mode == R_MULTICHANNEL) {
                    channel->frequency = devs[i]["channels"][j]["freq"];
                } else { /* R_SCAN */
                    channel->freq_count = devs[i]["channels"][j]["freqs"].getLength();
                    if(channel->freq_count < 1) {
                        cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: freqs should be a list with at least one element\n";
                        error();
                    }
                    channel->freqlist = (int *)malloc(channel->freq_count * sizeof(int));
                    if(channel->freqlist == NULL) {
                        cerr<<"Cannot allocate memory for freqlist\n";
                        error();
                    }
                    for(int f = 0; f<channel->freq_count; f++) {
                        channel->freqlist[f] = (int)(devs[i]["channels"][j]["freqs"][f]);
                    }
// Set initial frequency for scanning
// We tune 2 FFT bins higher to avoid DC spike
                    channel->frequency = channel->freqlist[0];
                    dev->centerfreq = channel->freqlist[0] + 2 * (double)(SOURCE_RATE / FFT_SIZE);
                }
                channel->username = strdup(devs[i]["channels"][j]["username"]);
                channel->password = strdup(devs[i]["channels"][j]["password"]);
                if(devs[i]["channels"][j].exists("name"))
                    channel->name = strdup(devs[i]["channels"][j]["name"]);
                if(devs[i]["channels"][j].exists("genre"))
                    channel->genre = strdup(devs[i]["channels"][j]["genre"]);
                dev->bins[j] = (int)ceil((channel->frequency + SOURCE_RATE - dev->centerfreq) / (double)(SOURCE_RATE / FFT_SIZE) - 1.0f) % FFT_SIZE;
            }
        }
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
    for (int i = 0; i < device_count; i++) {
        device_t* dev = devices + i;
        for (int j = 0; j < dev->channel_count; j++)  {
            channel_t* channel = dev->channels + j;
            mp3_setup(channel);
        }
#ifdef _WIN32
        dev->rtl_thread = (THREAD)_beginthread(rtlsdr_exec, 0, dev);
        if(dev->mode == R_SCAN)
            dev->controller_thread = (THREAD)_beginthread(controller_thread, 0, dev);
#else
        pthread_create(&dev->rtl_thread, NULL, &rtlsdr_exec, dev);
        if(dev->mode == R_SCAN)
// FIXME: not needed when freq_count == 1?
            pthread_create(&dev->controller_thread, NULL, &controller_thread, dev);
#endif
    }

    while (device_opened != device_count) {
        SLEEP(100);
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
    thread2 = (THREAD)_beginthread(mp3_check, 0, NULL);
#else
    pthread_create(&thread2, NULL, &mp3_check, NULL);
    THREAD thread3;
    pthread_create(&thread3, NULL, &mp3_thread, NULL);
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
