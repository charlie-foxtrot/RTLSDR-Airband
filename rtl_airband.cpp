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
#define SLEEP(x) Sleep(x)
#define THREAD HANDLE
#define GOTOXY(x, y) COORD xy; xy.X = x; xy.Y = y; SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), xy)
#define scanf scanf_s
#define sscanf sscanf_s
#define fscanf fscanf_s
#else
#include "hello_fft/mailbox.h"
#include "hello_fft/gpu_fft.h"
#define ALIGN
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
#define MP3_RATE 16000
#define AGC_EXTRA 48
#define FFT_SIZE 2048
#define CHANNELS 8
#else
#define BUF_SIZE 2560000
#define SOURCE_RATE 2560000
#define WAVE_RATE 8000
#define WAVE_BATCH 1000
#define MP3_RATE 8000
#define AGC_EXTRA 48
#define FFT_SIZE 512
#define FFT_SIZE_LOG 9
#define FFT_BATCH 250
#define CHANNELS 8
#endif
using namespace std;

char stream_hostname[256];
int stream_port;
char stream_username[256];
char stream_password[256];
char stream_mountpoints[CHANNELS][100];
int freqs[CHANNELS];
int bins[CHANNELS];
int avx;

void error() {
#ifdef _WIN32
	system("pause");
#endif
	exit(1);
}
unsigned char * buffer;
int bufs = 0, bufe;

rtlsdr_dev_t * dev;

void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
	memcpy(buffer + bufe, buf, len);
	if (bufe == 0) {
		memcpy(buffer + BUF_SIZE, buf, FFT_SIZE * 2);
	}
	bufe = bufe + len;
	if (bufe == BUF_SIZE) bufe = 0;
}

#ifdef _WIN32
void rtlsdr_exec(void* params) {
#else
void* rtlsdr_exec(void* params) {
#endif
	// params is int[3], params[0] = devindex, params[1] = center frequency, params[2] = gain
	int r;
	int device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stderr, "No supported devices found.\n");
		error();
	}

	printf("Found %d device(s).\n", device_count);

	int* p = (int*) params;
	rtlsdr_open(&dev, p[0]);
	if (NULL == dev) {
		fprintf(stderr, "Failed to open rtlsdr device #%d.\n", p[0]);
		error();
	}
	rtlsdr_set_sample_rate(dev, SOURCE_RATE);
	rtlsdr_set_center_freq(dev, p[1]);
	rtlsdr_set_tuner_gain_mode(dev, 1);
	rtlsdr_set_tuner_gain(dev, p[2]);
	rtlsdr_set_agc_mode(dev, 0);
	rtlsdr_reset_buffer(dev);
	r = rtlsdr_read_async(dev, rtlsdr_callback, NULL, 40, 25600);
}

FILE* oggfiles[CHANNELS];
shout_t * shout[CHANNELS];

lame_t lame[CHANNELS];
unsigned char lamebuf[22000];
FILE* lamefiles[CHANNELS];

void mp3_setup(int channel) {
	int ret;
	shout_t * shouttemp = shout_new();
	if (shouttemp == NULL) {
		printf("cannot allocate\n");
	}
	if (shout_set_host(shouttemp, stream_hostname) != SHOUTERR_SUCCESS) {
		printf("cannot set host %s\n", shout_get_error(shouttemp));
	}
	if (shout_set_protocol(shouttemp, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS) {
		printf("cannot set protocol %d\n", shout_get_error(shouttemp));
	}
	if (shout_set_port(shouttemp, stream_port) != SHOUTERR_SUCCESS) {
		printf("cannot set port %d\n", shout_get_error(shouttemp));
	}
	char mp[100];
#ifdef _WIN32
	sprintf_s(mp, 80, "/%s", stream_mountpoints[channel]);
#else
	sprintf(mp, "/%s", stream_mountpoints[channel]);
#endif
	if (shout_set_mount(shouttemp, mp) != SHOUTERR_SUCCESS) {
		printf("cannot set mount %d\n", shout_get_error(shouttemp));
	}
	if (shout_set_user(shouttemp, stream_username) != SHOUTERR_SUCCESS) {
		printf("cannot set user %d\n", shout_get_error(shouttemp));
	}
	if (shout_set_password(shouttemp, stream_password) != SHOUTERR_SUCCESS) {
		printf("cannot set password %d\n", shout_get_error(shouttemp));
	}
	if (shout_set_format(shouttemp, SHOUT_FORMAT_MP3) != SHOUTERR_SUCCESS){
		printf("cannot set format %d\n", shout_get_error(shouttemp));
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
		lame[channel] = lame_init();
		lame_set_in_samplerate(lame[channel], WAVE_RATE);
		lame_set_VBR(lame[channel], vbr_off);
		lame_set_brate(lame[channel], 16);
		lame_set_out_samplerate(lame[channel], MP3_RATE);
		lame_set_num_channels(lame[channel], 1);
		lame_set_mode(lame[channel], MONO);
		lame_init_params(lame[channel]);
		SLEEP(100);
		shout[channel] = shouttemp;
	} else {
		printf("Channel %d failed to connect %d\n", channel, ret);
		shout_free(shouttemp);
		return;
	}
}

void mp3_process(int channel, float* data, int size) {
	if (shout[channel] == NULL) {
		return;
	}
	int bytes = lame_encode_buffer_ieee_float(lame[channel], data, NULL, size, lamebuf, 22000);
	if (bytes > 0) {
		int ret = shout_send(shout[channel], lamebuf, bytes);
		if (ret < 0) {
			// reset connection
			shout_close(shout[channel]);
			shout_free(shout[channel]);
			shout[channel] = NULL;
			lame_close(lame[channel]);
			lame[channel] = NULL;
		}
	}
}

// reconnect as required
#ifdef _WIN32
void mp3_check(void* params) {
#else
void* mp3_check(void* params) {
#endif
	while (true) {
		SLEEP(5000);
		for (int i = 0; i < CHANNELS; i++) {
			if (freqs[i] != 0 && shout[i] == NULL){
				mp3_setup(i);
			}
		}
	}
}

fftwf_complex* fftin;
fftwf_complex* fftout;
ALIGN float window[FFT_SIZE * 2];
ALIGN float levels[256];
float window2[FFT_SIZE][256];
float waves[CHANNELS][WAVE_BATCH + FFT_SIZE * 2];
float waves2[CHANNELS][WAVE_BATCH + FFT_SIZE * 2];

void demodulate() {

	// initialize fft engine

#ifdef _WIN32
	fftwf_plan fft;
	fftin = fftwf_alloc_complex(FFT_SIZE);
	fftout = fftwf_alloc_complex(FFT_SIZE);

	fft = fftwf_plan_dft_1d(FFT_SIZE, fftin, fftout, FFTW_FORWARD, FFTW_MEASURE);

	for (int i=0; i<256; i++) {
		levels[i] = i-127.5f;
	}
#else
	int mb = mbox_open();
	struct GPU_FFT *fft;
	int ret = gpu_fft_prepare(mb, FFT_SIZE_LOG, GPU_FFT_FWD, FFT_BATCH, &fft);
	switch (ret) {
	case 0: printf("GPU initialized\n"); break;
	case -1: printf("Unable to enable V3D. Please check your firmware is up to date.\n"); return;
	case -2: printf("log2_N=%d not supported.  Try between 8 and 17.\n", FFT_SIZE_LOG); return;
	case -3: printf("Out of memory.  Try a smaller batch or increase GPU memory.\n"); return;
	}
#endif
	
	// initialize fft window
	// blackman 7
	// for raspberry pi, the whole matrix is computed

	const double a0 = 0.27105140069342f;
	const double a1 = 0.43329793923448f;   const double a2 = 0.21812299954311f;
	const double a3 = 0.06592544638803f;   const double a4 = 0.01081174209837f;
	const double a5 = 0.00077658482522f;   const double a6 = 0.00001388721735f;

	for (int i = 0; i < FFT_SIZE; i++) {
		double x = a0 - (a1 * cos((2.0 * M_PI * i) / FFT_SIZE))
			+ (a2 * cos((4.0 * M_PI * i) / FFT_SIZE))
			- (a3 * cos((6.0 * M_PI * i) / FFT_SIZE))
			+ (a4 * cos((8.0 * M_PI * i) / FFT_SIZE))
			- (a5 * cos((10.0 * M_PI * i) / FFT_SIZE))
			+ (a6 * cos((12.0 * M_PI * i) / FFT_SIZE));
#ifdef _WIN32
		window[i * 2] = window[i * 2 + 1] = (float)x;
#else
		for (int j = 0; j < 256; j++) {
			window2[i][j] = j*x;
		}
#endif
	}

	// speed2 = number of bytes per wave sample (x 2 for I and Q)
	int speed2 = (SOURCE_RATE * 2) / WAVE_RATE;
	int wavecount = 0;

	int agcsq[CHANNELS];		// squelch status, 0 = signal, 1 = suppressed
	char agcindicate[CHANNELS]; // squelch status indicator
	float agcavgfast[CHANNELS]; // average power, for AGC
	float agcavgslow[CHANNELS]; // average power, for squelch level detection
	float agcmin[CHANNELS];     // noise level
	ALIGN float wavessqrt[WAVE_BATCH + AGC_EXTRA * 2]; // normalized (capped) power

	for (int i = 0; i < CHANNELS; i++) {
		agcsq[i] = 1;
		agcavgfast[i] = 0.5;
		agcavgslow[i] = 0.5;
		agcmin[i] = 40;
		agcindicate[i] = ' ';
		for (int j = 0; j < AGC_EXTRA; j++) {
			waves[i][j] = 20;
			waves2[i][j] = 0.5;
		}
	}
	
	int row = 2;
	while (true) {
		int available = bufe - bufs;
		if (bufe < bufs) {
			available = (BUF_SIZE - bufe) + bufs;
		}
#ifdef _WIN32
		if (available < speed2 + FFT_SIZE * 2) {
			SLEEP(20);
			continue;
		}
		
		// process 4 rtl samples (16 bytes)
		if (avx) {
			for (int i = 0; i < FFT_SIZE; i += 8) {
				unsigned char* buf2 = buffer + bufs + i * 2;
				__m256 a = _mm256_set_ps(levels[*(buf2+7)], levels[*(buf2+6)], levels[*(buf2+5)], levels[*(buf2+4)], levels[*(buf2+3)], levels[*(buf2+2)], levels[*(buf2+1)], levels[*(buf2)]);
				__m256 b = _mm256_load_ps(&window[i * 2]);
				a = _mm256_mul_ps(a, b);
				_mm_prefetch((const CHAR *)&window[i * 2 + 128], _MM_HINT_T0);
				_mm_prefetch((const CHAR *)buf2 + 128, _MM_HINT_T0);
				_mm256_store_ps(&fftin[i][0], a);

			}
		} else {
			for (int i = 0; i < FFT_SIZE; i += 4) {
				unsigned char* buf2 = buffer + bufs + i * 2;
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
			for (int j = 0; j < CHANNELS; j++) {
				__m256 a = _mm256_loadu_ps(&fftout[bins[j]][0]);
				a = _mm256_mul_ps(a, a);
				a = _mm256_hadd_ps(a, a);
				a = _mm256_sqrt_ps(a);
				a = _mm256_hadd_ps(a, a);
				waves[j][wavecount] = a.m256_f32[0] + a.m256_f32[4];
			}
		} else {
			for (int j = 0; j < CHANNELS; j++) {
				__m128 a = _mm_loadu_ps(&fftout[bins[j]][0]);
				__m128 b = _mm_loadu_ps(&fftout[bins[j]+2][0]);
				a = _mm_mul_ps(a, a);
				b = _mm_mul_ps(b, b);
				a = _mm_hadd_ps(a, b);
				a = _mm_sqrt_ps(a);
				waves[j][wavecount] = a.m128_f32[0] + a.m128_f32[1] + a.m128_f32[2] + a.m128_f32[3];
			}
		}
		bufs += speed2;
		wavecount++;

#else

		if (available < speed2 * FFT_BATCH + FFT_SIZE * 2) {
			SLEEP(20);
			continue;
		}

		struct GPU_FFT_COMPLEX* base;
		unsigned char* bs2;
		float* w0;
		for (int i = 0; i < FFT_SIZE; i += 8) {
			base = fft->in + i;
			bs2 = buffer + bufs + i * 2;
			w0 = window2[i];
			for (int j = 0; j < FFT_BATCH; j++) {
				__builtin_prefetch(bs2 + speed2);
				unsigned char t0 = bs2[0];
				unsigned char t1 = bs2[1];
				unsigned char t2 = bs2[2];
				unsigned char t3 = bs2[3];
				float s0 = w0[t0];
				float s1 = w0[t1];
				w0 += 256;
				t0 = bs2[4];
				t1 = bs2[5];
				float s2 = w0[t2];
				float s3 = w0[t3];
				w0 += 256;

				t2 = bs2[6];
				t3 = bs2[7];
				base[0].re = s0;
				base[0].im = s1;
				s0 = w0[t0];
				s1 = w0[t1];
				w0 += 256;

				t0 = bs2[8];
				t1 = bs2[9];
				base[1].re = s2;
				base[1].im = s3;
				s2 = w0[t2];
				s3 = w0[t3];
				w0 += 256;

				t2 = bs2[10];
				t3 = bs2[11];
				base[2].re = s0;
				base[2].im = s1;
				s0 = w0[t0];
				s1 = w0[t1];
				w0 += 256;

				t0 = bs2[12];
				t1 = bs2[13];
				base[3].re = s2;
				base[3].im = s3;
				s2 = w0[t2];
				s3 = w0[t3];
				w0 += 256;

				t2 = bs2[14];
				t3 = bs2[15];
				base[4].re = s0;
				base[4].im = s1;
				s0 = w0[t0];
				s1 = w0[t1];
				w0 += 256;

				base[5].re = s2;
				base[5].im = s3;
				s2 = w0[t2];
				s3 = w0[t3];
				base[6].re = s0;
				base[6].im = s1;
				w0 -= 1792;
				base[7].re = s2;
				base[7].im = s3;
				base += fft->step;
				bs2 += speed2;
			}
		}
		gpu_fft_execute(fft);

		bufs += speed2 * FFT_BATCH;
		for (int j = 0; j < CHANNELS; j++) {
			base = fft->out;
			if (freqs[j] == 0) continue;
			for (int i = 0; i < FFT_BATCH; i++) {
				waves[j][wavecount + i] = sqrt(base[bins[j]].re * base[bins[j]].re + base[bins[j]].im * base[bins[j]].im) +
					sqrt(base[bins[j] + 1].re * base[bins[j] + 1].re + base[bins[j] + 1].im * base[bins[j] + 1].im);
				base += fft->step;
			}
		}
		wavecount += FFT_BATCH;
#endif

		if (wavecount >= WAVE_BATCH + AGC_EXTRA) {
			GOTOXY(0, row);
			for (int i = 0; i < CHANNELS; i++) {
				if (freqs[i] == 0) continue;
#ifdef _WIN32
				if (avx) {
					__m256 agccap = _mm256_set1_ps(agcmin[i] * 4.5f);
					for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 8) {
						__m256 t = _mm256_load_ps(&waves[i][j]);
						_mm256_store_ps(&wavessqrt[j], _mm256_min_ps(t, agccap));
					}
				} else {
					__m128 agccap = _mm_set1_ps(agcmin[i] * 4.5f);
					for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 4) {
						__m128 t = _mm_load_ps(&waves[i][j]);
						_mm_store_ps(&wavessqrt[j], _mm_min_ps(t, agccap));
					}
				}
#else
				float agcmin2 = agcmin[i] * 4.5f;
				for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j++) {
					wavessqrt[j] = min(waves[i][j], agcmin2);
				}
#endif
				int agcsmall = 0;
				for (int j = AGC_EXTRA; j < WAVE_BATCH + AGC_EXTRA; j++) {
					// auto noise floor
					if (j % 16 == 0) {
						agcmin[i] = agcmin[i] * 0.97f + min(agcavgslow[i], agcmin[i]) * 0.03f + 0.0001f;
					}

					// average power
					agcavgslow[i] = agcavgslow[i] * 0.99f + wavessqrt[j] * 0.01f;

					if (agcsq[i] > 0) {
						agcsq[i] = max(agcsq[i] - 1, 1);
						if (agcsq[i] == 1 && agcavgslow[i] > 3.0f * agcmin[i]) {
							agcsq[i] = -AGC_EXTRA;
							agcindicate[i] = '*';
							for (int k = j - AGC_EXTRA; k < j; k++) {
								if (waves[i][k] > agcmin[i] * 3.0f) {
									agcavgfast[i] = agcavgfast[i] * 0.98f + waves[i][k] * 0.02f;
								}
							}
						}
					}
					else {
						if (waves[i][j] > agcmin[i] * 3.0f) {
							agcavgfast[i] = agcavgfast[i] * 0.995f + waves[i][j] * 0.005f;
							agcsmall = 0;
						}
						else {
							agcsmall++;
						}
						agcsq[i] = min(agcsq[i] + 1, -1);
						if (agcsq[i] == -1 && agcavgslow[i] < 2.4f * agcmin[i] || agcsmall == AGC_EXTRA - 12) {
							agcsq[i] = AGC_EXTRA * 2;
							agcindicate[i] = ' ';
							for (int k = j - AGC_EXTRA + 1; k < j; k++) {
								waves2[i][k] = waves2[i][k - 1] * 0.94f;
							}
						}
					}
					waves2[i][j] = agcsq[i] > 0 ? 0 : (waves[i][j - AGC_EXTRA] - agcavgfast[i]) / (agcavgfast[i] * (3.7f - 0.2f * log(agcavgfast[i])));
					if (abs(waves2[i][j]) > 0.8f) {
						waves2[i][j] *= 0.85f;
						agcavgfast[i] *= 1.15f;
					}
				}
				mp3_process(i, waves2[i], WAVE_BATCH);
				memcpy(waves[i], waves[i] + WAVE_BATCH, (wavecount - WAVE_BATCH) * 4);
				memcpy(waves2[i], waves2[i] + WAVE_BATCH, (wavecount - WAVE_BATCH) * 4);
				printf("%4.0f/%2.0f%c  ", agcavgslow[i], agcmin[i], agcindicate[i]);
			}
			printf("\n");
			row++;
			if (row == 20) row = 2;
			wavecount -= WAVE_BATCH;
		}

		if (bufs >= BUF_SIZE) bufs -= BUF_SIZE;

	}
}

int main(int argc, char* argv[]) {

	uintptr_t tempptr = (uintptr_t)malloc(BUF_SIZE + FFT_SIZE * 2 + 15);
	tempptr &= ~0x0F;
	buffer = (unsigned char *)tempptr;

#ifdef _WIN32
	// check cpu features
	int cpuinfo[4];
	__cpuid(cpuinfo, 1);
	if (cpuinfo[2] & 1 << 28) {
		avx = 1;
		printf("AVX support detected.\n");
	} else if (cpuinfo[1] & 1) {
		avx = 0;
		printf("SSE3 suport detected.\n");
	} else {
		printf("Unsupported CPU.\n");
		error();
	}
#endif
	
	int devindex;
	char config[256];
	if (argc == 3) {
		if (sscanf(argv[1], "%d", &devindex) < 1 || sscanf(argv[2], "%80s", &config, 80) < 1) {
			printf("Usage: rtl_airband <device> <config>\n");
			error();
		}
	} else if (argc != 1) {
		printf("Usage: rtl_airband <device> <config>\n");
		error();
	} else {
		printf("Device index: ");
		if (scanf("%d", &devindex) < 1) {
			printf("Invalid device index\n");
			error();
		}
		printf("Config Name: ");
		if (scanf("%80s", config, 80) < 1) {
			printf("Invalid config name\n");
			error();
		}
	}
	char filename[100];
#ifdef _WIN32
	sprintf_s(filename, 80, "config/%s.txt", config);
	FILE* f;
	if (fopen_s(&f, filename, "r") != 0) {
		printf("Config %s not found.\n", config);
		error();
	}
#else
	sprintf(filename, "config/%s.txt", config);
	FILE* f = fopen(filename, "r");
	if (f == NULL) {
		printf("Config %s not found.\n", config);
		error();
	}
#endif

	fscanf(f, "%120s\n", stream_hostname, 120);
	fscanf(f, "%d\n", &stream_port);
	fscanf(f, "%80s\n", stream_username, 80);
	fscanf(f, "%80s\n", stream_password, 80);

	int center, correction, gain;
	fscanf(f, "%d\n", &center);
	fscanf(f, "%d\n", &correction);
	fscanf(f, "%d\n", &gain);
	for (int i = 0; i < CHANNELS; i++)  {
		fscanf(f, "%d ", &freqs[i]);
		if (freqs[i] != 0) {
			fscanf(f, "%80s", stream_mountpoints[i], 80);
		} else {
			continue;
		}
#ifdef _WIN32
		bins[i] = (int)ceil((freqs[i] + SOURCE_RATE - center + correction) / (double)(SOURCE_RATE / FFT_SIZE) - 2.0f) % FFT_SIZE;
#else
		bins[i] = (int)ceil((freqs[i] + SOURCE_RATE - center + correction) / (double)(SOURCE_RATE / FFT_SIZE) - 1.0f) % FFT_SIZE;
#endif
	}
	fclose(f);

	shout_init();
	for (int i = 0; i < CHANNELS; i++) {
		if (freqs[i] == 0) continue;
		mp3_setup(i);
	}

	THREAD thread1, thread2;
	int rtlargs[2];
	rtlargs[0] = devindex;
	rtlargs[1] = center;
	rtlargs[2] = gain;
#ifdef _WIN32
	thread1 = (THREAD)_beginthread(rtlsdr_exec, 0, rtlargs);
#else
	pthread_create(&thread1, NULL, &rtlsdr_exec, rtlargs);
#endif
	printf("Starting RTLSDR...\n");
	while (bufe == 0) {
		SLEEP(100);
	}

#ifdef _WIN32
	system("cls");
#else
	printf("\e[1;1H\e[2J");
#endif
	
	GOTOXY(0, 0);
	for (int i = 0; i < CHANNELS; i++) {
		if (freqs[i] != 0)
			printf(" %7.3f  ", freqs[i] / 1000000.0);
	}
	printf("\n                                           \n");

#ifdef _WIN32
	thread2 = (THREAD)_beginthread(mp3_check, 0, NULL);
#else
	pthread_create(&thread2, NULL, &mp3_check, NULL);
#endif

	demodulate();

	return 0;
}