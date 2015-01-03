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
#else
#include "hello_fft/mailbox.h"
#include "hello_fft/gpu_fft.h"
#define ALIGN
#define ALIGN2 __attribute__((aligned(32)))
#define SLEEP(x) usleep(x * 1000)
#define THREAD pthread_t
#define GOTOXY(x, y) printf("%c[%d;%df",0x1B,y,x)
#include <unistd.h>
#include <pthread.h>
#include <algorithm>
#endif  

#include <cstring>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>

#include <fftw3.h>
#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>
#include <shout/shout.h>
#ifdef _WIN32
#include <lame.h>
#else
#include <lame/lame.h>
#endif

#include <rtl-sdr.h>

#ifdef _WIN32

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#define BUF_SIZE 2560000
#define SOURCE_RATE 2560000
#define WAVE_RATE 8000
#define WAVE_BATCH 1000
#define WAVE_LEN 2048
#define MP3_RATE 16000
#define AGC_EXTRA 48
#define FFT_SIZE 2048
#define FFT_BATCH 1
#define CHANNELS 8
#else
#define BUF_SIZE 2560000
#define SOURCE_RATE 2560000
#define WAVE_RATE 8000
#define WAVE_BATCH 1000
#define WAVE_LEN 2048
#define MP3_RATE 8000
#define AGC_EXTRA 48
#define FFT_SIZE 512
#define FFT_SIZE_LOG 9
#define FFT_BATCH 250
#define CHANNELS 8

extern "C" void samplefft(GPU_FFT_COMPLEX* dest, unsigned char* buffer, float* window, float* levels);
extern "C" void fftwave(float* dest, GPU_FFT_COMPLEX* src, int* sizes, int* bins);

#endif
using namespace std;

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

    char hostname[256];
    int port;
    char username[256];
    char password[256];
    char mountpoint[256];
    shout_t * shout;
    lame_t lame;
};

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
    THREAD thread;
    int row;
};

device_t* devices;
int device_count;
int device_opened = 0;
int avx;
int quiet;

void error() {
#ifdef _WIN32
    system("pause");
#endif
    exit(1);
}

void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
    device_t *dev = (device_t*)ctx;
    memcpy(dev->buffer + dev->bufe, buf, len);
    if (dev->bufe == 0) {
        memcpy(dev->buffer + BUF_SIZE, buf, FFT_SIZE * 2);
    }
    dev->bufe = dev->bufe + len;
    if (dev->bufe == BUF_SIZE) dev->bufe = 0;
}

#ifdef _WIN32
void rtlsdr_exec(void* params) {
#else
void* rtlsdr_exec(void* params) {
#endif
    device_t *dev = (device_t*)params;
    int r;
    rtlsdr_open(&dev->rtlsdr, dev->device);
    if (NULL == dev) {
        fprintf(stderr, "Failed to open rtlsdr device #%d.\n", dev->device);
        error();
        return NULL;
    }
    rtlsdr_set_sample_rate(dev->rtlsdr, SOURCE_RATE);
    rtlsdr_set_center_freq(dev->rtlsdr, dev->centerfreq);
    rtlsdr_set_tuner_gain_mode(dev->rtlsdr, 1);
    rtlsdr_set_tuner_gain(dev->rtlsdr, dev->gain);
    rtlsdr_set_agc_mode(dev->rtlsdr, 0);
    rtlsdr_reset_buffer(dev->rtlsdr);
    printf("Device %d started.\n", dev->device);
    device_opened++;
    r = rtlsdr_read_async(dev->rtlsdr, rtlsdr_callback, params, 20, 320000);
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
    char samplerates[20];
#ifdef _WIN32
    sprintf_s(samplerates, 15, "%d", MP3_RATE);
#else
    sprintf(samplerates, "%d", MP3_RATE);
#endif
    shout_set_audio_info(shouttemp, SHOUT_AI_SAMPLERATE, samplerates);
    shout_set_audio_info(shouttemp, SHOUT_AI_CHANNELS, "1");

    ret = shout_open(shouttemp);
    if (ret == SHOUTERR_SUCCESS) {
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
        if (ret < 0) {
            // reset connection
            shout_close(channel->shout);
            shout_free(channel->shout);
            channel->shout = NULL;
            lame_close(channel->lame);
            channel->lame = NULL;
        }
    }
}

#ifndef _WIN32
pthread_cond_t      mp3_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t     mp3_mutex = PTHREAD_MUTEX_INITIALIZER;
void* mp3_thread(void* params) {
    while (true) {
        pthread_cond_wait(&mp3_cond, &mp3_mutex);
        for (int i = 0; i < device_count; i++) {
            if (devices[i].waveavail) {
                devices[i].waveavail = 0;
                for (int j = 0; j < devices[i].channel_count; j++) {
                    channel_t* channel = devices[i].channels + j;
                    mp3_process(channel);
                    memcpy(channel->waveout, channel->waveout + WAVE_BATCH, AGC_EXTRA * 4);
                }
            }
        }
    }
}
#endif

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
                if (dev->channels[j].shout == NULL){
                    mp3_setup(dev->channels + j);
                }
            }
        }
    }
}

void demodulate() {

    // initialize fft engine
#ifdef _WIN32
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
        case -1: printf("Unable to enable V3D. Please check your firmware is up to date.\n"); error();
        case -2: printf("log2_N=%d not supported.  Try between 8 and 17.\n", FFT_SIZE_LOG); error();
        case -3: printf("Out of memory.  Try a smaller batch or increase GPU memory.\n"); error();
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
    // for raspberry pi, the whole matrix is computed
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
        device_t* dev = devices + device_num;
        int available = dev->bufe - dev->bufs;
        if (dev->bufe < dev->bufs) {
            available = (BUF_SIZE - dev->bufe) + dev->bufs;
        }

        if (available < speed2 * FFT_BATCH + FFT_SIZE * 2) {
            // move to next device
            device_num = (device_num + 1) % device_count;
            SLEEP(10);
            continue;
        }

#ifdef _WIN32
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

        fftwf_execute(fft);

        // sum up the power of 4 bins 
        // windows: SAMPLE_RATE = 2.56M and FFT_SIZE = 2048, so width = 5 kHz
        if (avx) {
            for (int j = 0; j < dev->channel_count; j++) {
                __m256 a = _mm256_loadu_ps(&fftout[dev->bins[j]][0]);
                a = _mm256_mul_ps(a, a);
                a = _mm256_hadd_ps(a, a);
                a = _mm256_sqrt_ps(a);
                a = _mm256_hadd_ps(a, a);
                dev->channels[j].wavein[dev->waveend] = a.m256_f32[0] + a.m256_f32[4];
            }
        } else {
            for (int j = 0; j < dev->channel_count; j++) {
                __m128 a = _mm_loadu_ps(&fftout[dev->bins[j]][0]);
                __m128 b = _mm_loadu_ps(&fftout[dev->bins[j] + 2][0]);
                a = _mm_mul_ps(a, a);
                b = _mm_mul_ps(b, b);
                a = _mm_hadd_ps(a, b);
                a = _mm_sqrt_ps(a);
                dev->channels[j].wavein[dev->waveend] = a.m128_f32[0] + a.m128_f32[1] + a.m128_f32[2] + a.m128_f32[3];
            }
        }
#else
        for (int i = 0; i < FFT_BATCH; i++) {
            samplefft(fft->in + i * fft->step, dev->buffer + dev->bufs + i * speed2, window, levels);
        }

        // allow mp3 encoding thread to run while waiting for GPU to finish
        pthread_cond_signal(&mp3_cond);

        gpu_fft_execute(fft);

        fftwave(dev->channels[0].wavein + dev->waveend, fft->out, sizes, dev->bins);
#endif
        dev->waveend += FFT_BATCH;
        
        if (dev->waveend >= WAVE_BATCH + AGC_EXTRA) {
            if (!quiet) {
                GOTOXY(0, device_num * 17 + dev->row + 3);
            }
            for (int i = 0; i < dev->channel_count; i++) {
                channel_t* channel = dev->channels + i;
#ifdef _WIN32
                if (avx) {
                    __m256 agccap = _mm256_set1_ps(channel->agcmin * 4.5f);
                    for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 8) {
                        __m256 t = _mm256_loadu_ps(channel->wavein + j);
                        _mm256_storeu_ps(channel->waveref + j, _mm256_min_ps(t, agccap));
                    }
                } else {
                    __m128 agccap = _mm_set1_ps(channel->agcmin * 4.5f);
                    for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 4) {
                        __m128 t = _mm_loadu_ps(channel->wavein + j);
                        _mm_storeu_ps(channel->waveref + j, _mm_min_ps(t, agccap));
                    }
                }
#else
                float agcmin2 = channel->agcmin * 4.5f;
                for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j++) {
                    channel->waveref[j] = min(channel->wavein[j], agcmin2);
                }
#endif
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
                        if (channel->agcsq == -1 && channel->agcavgslow < 2.4f * channel->agcmin || channel->agclow == AGC_EXTRA - 12) {
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
                if (!quiet) {
                    printf("%4.0f/%3.0f%c ", channel->agcavgslow, channel->agcmin, channel->shout == NULL ? 'X' : channel->agcindicate);
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
        // always rotate to next device for rpi
        device_num = (device_num + 1) % device_count;
#endif
    }
}

int main(int argc, char* argv[]) {

#ifdef _WIN32
    // check cpu features
    int cpuinfo[4];
    __cpuid(cpuinfo, 1);
    if (cpuinfo[2] & 1 << 28) {
        avx = 1;
        printf("AVX support detected.\n");
    } else if (cpuinfo[2] & 1) {
        avx = 0;
        printf("SSE3 suport detected.\n");
    } else {
        printf("Unsupported CPU.\n");
        error();
    }
#endif

    quiet = (argc > 0) && (argv[1] != NULL) && (strncmp(argv[1], "--quiet", 10) == 0);

    printf("Reading config.\n");
    // read config
    FILE* f;
#ifdef _WIN32
    if (fopen_s(&f, "config.txt", "r") != 0) {
#else
    f = fopen("config.txt", "r");
    if (f == NULL) {
#endif
        printf("Config config.txt not found.\nStarting from 2014-07-05 a new config file format is required.\n");
        printf("Visit https ://www.github.com/microtony/RTLSDR-Airband for details.\n");
        error();
    }
    fscanf(f, "%d\n", &device_count);
    if (device_count < 1) {
        printf("Device count is less than 1?\n");
        error();
    }
    int device_count2 = rtlsdr_get_device_count();
    if (!device_count2) {
        fprintf(stderr, "No supported devices found.\n");
        error();
    } else if (device_count2 < device_count) {
        fprintf(stderr, "Not enough devices... (only %d detected)\n", device_count2);
        error();
    } else {
        fprintf(stderr, "%d device(s) found.\n", device_count2);
    }

    printf("Allocating memory\n");
    uintptr_t tempptr = (uintptr_t)malloc(device_count * sizeof(device_t)+31);
    tempptr &= ~0x0F;
    devices = (device_t *)tempptr;
    shout_init();

    printf("Starting devices\n");
    for (int i = 0; i < device_count; i++) {
        device_t* dev = devices + i;
        if (dev->device >= device_count2) {
            fprintf(stderr, "Specified device id %d is >= number of devices %d...\n", dev->device, device_count2);
            error();
        }
        fscanf(f, "%d %d %d %d %d\n", &dev->device, &dev->channel_count, &dev->gain, &dev->centerfreq, &dev->correction);
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
#ifdef _WIN32
            fscanf_s(f, "%120s %d %120s %d %120s %120s\n", channel->hostname, 120, &channel->port, channel->mountpoint, 120, &channel->frequency, channel->username, 120, channel->password, 120);
            dev->bins[j] = (int)ceil((channel->frequency + SOURCE_RATE - dev->centerfreq + dev->correction) / (double)(SOURCE_RATE / FFT_SIZE) - 2.0f) % FFT_SIZE;
#else
            fscanf(f, "%120s %d %120s %d %120s %120s\n", channel->hostname, &channel->port, channel->mountpoint, &channel->frequency, channel->username, channel->password);
            dev->bins[j] = (int)ceil((channel->frequency + SOURCE_RATE - dev->centerfreq + dev->correction) / (double)(SOURCE_RATE / FFT_SIZE) - 1.0f) % FFT_SIZE;
#endif
            mp3_setup(channel);
        }
#ifdef _WIN32
        dev->thread = (THREAD)_beginthread(rtlsdr_exec, 0, dev);
#else
        pthread_create(&dev->thread, NULL, &rtlsdr_exec, dev);
#endif
    }
    fclose(f);

    while (device_opened != device_count) {
        SLEEP(100);
    }
    if (!quiet) {
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

    return 0;
}
