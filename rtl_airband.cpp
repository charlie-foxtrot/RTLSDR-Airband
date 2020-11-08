/*
 * RTLSDR AM/NFM demodulator, mixer, streamer and recorder
 *
 * Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
 * Copyright (c) 2015-2020 Tomasz Lemiech <szpajder@gmail.com>
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

#if defined USE_BCM_VC && !defined __arm__
#error Broadcom VideoCore support can only be enabled on ARM builds
#endif

// From this point we may safely assume that USE_BCM_VC implies __arm__

#if defined (__arm__) || defined (__aarch64__)

#ifdef USE_BCM_VC
#include "hello_fft/mailbox.h"
#include "hello_fft/gpu_fft.h"
#endif

#else	/* x86 */
#include <xmmintrin.h>
#endif	/* x86 */

#include <unistd.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#ifndef __MINGW32__
#include <sys/wait.h>
#else
#include <windows.h>
#endif

#include <algorithm>
#include <csignal>
#include <cstdarg>
#include <cerrno>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <libconfig.h++>
#include <stdint.h>			// uint8_t
#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>
#include <shout/shout.h>
#include <lame/lame.h>
#include "input-common.h"
#include "rtl_airband.h"

#ifdef WITH_PROFILING
#include "gperftools/profiler.h"
#endif

using namespace std;
using namespace libconfig;

device_t* devices;
mixer_t* mixers;
int device_count, mixer_count;
static int devices_running = 0;
int foreground = 0;			// daemonize
int tui = 0;				// do not display textual user interface
int do_syslog = 1;
int shout_metadata_delay = 3;
volatile int do_exit = 0;
bool use_localtime = false;
bool multiple_demod_threads = false;
bool log_scan_activity = false;
char *stats_filepath = NULL;
size_t fft_size_log = DEFAULT_FFT_SIZE_LOG;
size_t fft_size = 1 << fft_size_log;
#ifdef NFM
float alpha = exp(-1.0f/(WAVE_RATE * 2e-4));
enum fm_demod_algo {
	FM_FAST_ATAN2,
	FM_QUADRI_DEMOD
};
enum fm_demod_algo fm_demod = FM_FAST_ATAN2;
#endif

#if DEBUG
char *debug_path;
#endif
pthread_cond_t	mp3_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t	mp3_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifndef __MINGW32__
void sighandler(int sig) {
	log(LOG_NOTICE, "Got signal %d, exiting\n", sig);
	do_exit = 1;
}
#else
BOOL WINAPI sighandler(int signum) {
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		return TRUE;
	}
	return FALSE;
}
#endif


void* controller_thread(void* params) {
	device_t *dev = (device_t*)params;
	int i = 0;
	int consecutive_squelch_off = 0;
	int new_centerfreq = 0;
	struct timeval tv;

	if(dev->channels[0].freq_count < 2) return 0;
	while(!do_exit) {
		SLEEP(200);
		if(dev->channels[0].axcindicate == NO_SIGNAL) {
			if(consecutive_squelch_off < 10) {
				consecutive_squelch_off++;
			} else {
				i++; i %= dev->channels[0].freq_count;
				dev->channels[0].freq_idx = i;
				new_centerfreq = dev->channels[0].freqlist[i].frequency + 20 * (double)(dev->input->sample_rate / fft_size);
				if(input_set_centerfreq(dev->input, new_centerfreq) < 0) {
					break;
				}
			}
		} else {
			if(consecutive_squelch_off == 10) {
				if(log_scan_activity)
					log(LOG_INFO, "Activity on %7.3f MHz\n", dev->channels[0].freqlist[i].frequency / 1000000.0);
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

void multiply(float ar, float aj, float br, float bj, float *cr, float *cj)
{
	*cr = ar*br - aj*bj;
	*cj = aj*br + ar*bj;
}

#ifdef NFM
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
		angle = pi4 - pi4 * (x-yabs) / (x+yabs);
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
	const status _prev_axcindicate;

#ifdef USE_BCM_VC
	float square(const GPU_FFT_COMPLEX *fft_results, size_t index)
	{
		return fft_results[index].re * fft_results[index].re + fft_results[index].im * fft_results[index].im;
	}
#else
	float square(const fftwf_complex *fft_results, size_t index)
	{
		return fft_results[index][0] * fft_results[index][0] + fft_results[index][1] * fft_results[index][1];
	}
#endif
	template <class FFT_RESULTS, int STEP>
		size_t check(const FFT_RESULTS* fft_results, const size_t base, const float base_value, unsigned char afc)
	{
		float threshold = 0;
		size_t bin;
		for (bin = base;; bin+= STEP) {
			if (STEP < 0) {
			if (bin < -STEP)
				break;

			} else if ( (size_t)(bin + STEP) >= fft_size)
				break;

			const float value = square(fft_results, (size_t)(bin + STEP));
			if (value <= base_value)
				break;

			if (base == (size_t)bin) {
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
		if (axcindicate != NO_SIGNAL && _prev_axcindicate == NO_SIGNAL) {
			const size_t base = dev->base_bins[index];
			const float base_value = square(fft_results, base);
			size_t bin = check<FFT_RESULTS, -1>(fft_results, base, base_value, channel->afc);
			if (bin == base)
				bin = check<FFT_RESULTS, 1>(fft_results, base, base_value, channel->afc);

			if (dev->bins[index] != bin) {
#ifdef AFC_LOGGING
				log(LOG_INFO, "AFC device=%d channel=%d: base=%zu prev=%zu now=%zu\n", dev->device, index, base, dev->bins[index], bin);
#endif
				dev->bins[index] = bin;
				if ( bin > base )
					channel->axcindicate = AFC_UP;
				else if ( bin < base )
					channel->axcindicate = AFC_DOWN;
			}
		}
		else if (axcindicate == NO_SIGNAL && _prev_axcindicate != NO_SIGNAL)
			dev->bins[index] = dev->base_bins[index];
	}
};

void init_demod(demod_params_t *params, int start_device, int end_device) {
	assert(params != NULL);

	params->start_device = start_device;
	params->end_device = end_device;

#ifndef USE_BCM_VC
	params->fftin = fftwf_alloc_complex(fft_size);
	params->fftout = fftwf_alloc_complex(fft_size);
	params->fft = fftwf_plan_dft_1d(fft_size, params->fftin, params->fftout, FFTW_FORWARD, FFTW_MEASURE);
#endif
}

int next_device(demod_params_t *params, int current) {
	if (current == params->end_device) {
		return params->start_device;
	}
	return current + 1;
}

void *demodulate(void *params) {
	assert(params != NULL);
	demod_params_t *demod_params = (demod_params_t *) params;

	// initialize fft engine
#ifdef USE_BCM_VC
	int mb = mbox_open();
	struct GPU_FFT *fft;
	int ret = gpu_fft_prepare(mb, fft_size_log, GPU_FFT_FWD, FFT_BATCH, &fft);
	switch (ret) {
		case -1:
			log(LOG_CRIT, "Unable to enable V3D. Please check your firmware is up to date.\n");
			error();
			break;
		case -2:
			log(LOG_CRIT, "log2_N=%d not supported. Try between 8 and 17.\n", fft_size_log);
			error();
			break;
		case -3:
			log(LOG_CRIT, "Out of memory. Try a smaller batch or increase GPU memory.\n");
			error();
			break;
	}
#else
	fftwf_complex* fftin = demod_params->fftin;
	fftwf_complex* fftout = demod_params->fftout;
#endif
	ALIGN float ALIGN2 levels_u8[256], levels_s8[256];
	float *levels_ptr = NULL;

	for (int i=0; i<256; i++) {
		levels_u8[i] = i-127.5f;
	}
	for (int16_t i=-127; i<128; i++) {
		levels_s8[(uint8_t)i] = i;
	}

	// initialize fft window
	// blackman 7
	// the whole matrix is computed
	ALIGN float ALIGN2 window[fft_size * 2];
	const double a0 = 0.27105140069342f;
	const double a1 = 0.43329793923448f;	const double a2 = 0.21812299954311f;
	const double a3 = 0.06592544638803f;	const double a4 = 0.01081174209837f;
	const double a5 = 0.00077658482522f;	const double a6 = 0.00001388721735f;

	for (size_t i = 0; i < fft_size; i++) {
		double x = a0 - (a1 * cos((2.0 * M_PI * i) / (fft_size-1)))
			+ (a2 * cos((4.0 * M_PI * i) / (fft_size - 1)))
			- (a3 * cos((6.0 * M_PI * i) / (fft_size - 1)))
			+ (a4 * cos((8.0 * M_PI * i) / (fft_size - 1)))
			- (a5 * cos((10.0 * M_PI * i) / (fft_size - 1)))
			+ (a6 * cos((12.0 * M_PI * i) / (fft_size - 1)));
		window[i * 2] = window[i * 2 + 1] = (float)x;
	}

	struct timeval ts, te;
	if(DEBUG)
		gettimeofday(&ts, NULL);
	size_t available;
	int device_num = demod_params->start_device;
	while (true) {

		if(do_exit) {
#ifdef USE_BCM_VC
			log(LOG_INFO, "Freeing GPU memory\n");
			gpu_fft_release(fft);
#endif
			return NULL;
		}

		device_t* dev = devices + device_num;

		pthread_mutex_lock(&dev->input->buffer_lock);
		if(dev->input->bufe >= dev->input->bufs)
			available = dev->input->bufe - dev->input->bufs;
		else
			available = dev->input->buf_size - dev->input->bufs + dev->input->bufe;
		pthread_mutex_unlock(&dev->input->buffer_lock);

		if(devices_running == 0) {
			log(LOG_ERR, "All receivers failed, exiting\n");
			do_exit = 1;
			continue;
		}

		if (dev->input->state != INPUT_RUNNING) {
			if(dev->input->state == INPUT_FAILED) {
				dev->input->state = INPUT_DISABLED;
				disable_device_outputs(dev);
				devices_running--;
			}
			device_num = next_device(demod_params, device_num);
			continue;
		}

		// number of input bytes per output wave sample (x 2 for I and Q)
		size_t bps = 2 * dev->input->bytes_per_sample * (size_t)round((double)dev->input->sample_rate / (double)WAVE_RATE);
		if (available < bps * FFT_BATCH + fft_size * dev->input->bytes_per_sample * 2) {
			// move to next device
			device_num = next_device(demod_params, device_num);
			SLEEP(10);
			continue;
		}

		if(dev->input->sfmt == SFMT_S16) {
			float const scale = 127.5f / dev->input->fullscale;
#ifdef USE_BCM_VC
			struct GPU_FFT_COMPLEX *ptr = fft->in;
			for(size_t b = 0; b < FFT_BATCH; b++, ptr += fft->step) {
				short *buf2 = (short *)(dev->input->buffer + dev->input->bufs + b * bps);
				for(size_t i = 0; i < fft_size; i++, buf2 += 2) {
					ptr[i].re = scale * (float)buf2[0] * window[i*2];
					ptr[i].im = scale * (float)buf2[1] * window[i*2];
				}
			}
#else
			short *buf2 = (short *)(dev->input->buffer + dev->input->bufs);
			for(size_t i = 0; i < fft_size; i++, buf2 += 2) {
				 fftin[i][0] = scale * (float)buf2[0] * window[i*2];
				 fftin[i][1] = scale * (float)buf2[1] * window[i*2];
			}
#endif
		} else if(dev->input->sfmt == SFMT_F32) {
			float const scale = 127.5f / dev->input->fullscale;
#ifdef USE_BCM_VC
			struct GPU_FFT_COMPLEX *ptr = fft->in;
			for(size_t b = 0; b < FFT_BATCH; b++, ptr += fft->step) {
				float *buf2 = (float *)(dev->input->buffer + dev->input->bufs + b * bps);
				for(size_t i = 0; i < fft_size; i++, buf2 += 2) {
					ptr[i].re = scale * buf2[0] * window[i*2];
					ptr[i].im = scale * buf2[1] * window[i*2];
				}
			}
#else
			float *buf2 = (float *)(dev->input->buffer + dev->input->bufs);
			for(size_t i = 0; i < fft_size; i++, buf2 += 2) {
				fftin[i][0] = scale * buf2[0] * window[i*2];
				fftin[i][1] = scale * buf2[1] * window[i*2];
			}
#endif
		} else {	// S8 or U8
			levels_ptr = (dev->input->sfmt == SFMT_U8 ? levels_u8 : levels_s8);
#if defined USE_BCM_VC
			sample_fft_arg sfa = {fft_size / 4, fft->in};
			for (size_t i = 0; i < FFT_BATCH; i++) {
				samplefft(&sfa, dev->input->buffer + dev->input->bufs + i * bps, window, levels_ptr);
				sfa.dest+= fft->step;
			}
#elif defined (__arm__) || defined (__aarch64__)
			unsigned char* buf2 = dev->input->buffer + dev->input->bufs;
			for (size_t i = 0; i < fft_size; i++, buf2 += 2) {
				float re = levels_ptr[buf2[0]] * window[i*2];
				float im = levels_ptr[buf2[1]] * window[i*2];
				fftin[i][0] = re;
				fftin[i][1] = im;
			}
#else /* x86 */
			unsigned char* buf2 = dev->input->buffer + dev->input->bufs;
			for (size_t i = 0; i < fft_size; i += 2, buf2 += 4) {
				__m128 a = _mm_set_ps(levels_ptr[buf2[3]], levels_ptr[buf2[2]], levels_ptr[buf2[1]], levels_ptr[buf2[0]]);
				__m128 b = _mm_load_ps(&window[i * 2]);
				a = _mm_mul_ps(a, b);
				_mm_store_ps(&fftin[i][0], a);
			}
#endif
		}
#ifdef USE_BCM_VC
		gpu_fft_execute(fft);
#else
		fftwf_execute(demod_params->fft);
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
		for (int j = 0; j < dev->channel_count; j++) {
			if(dev->channels[j].needs_raw_iq) {
				struct GPU_FFT_COMPLEX *ptr = fft->out;
				for (int job = 0; job < FFT_BATCH; job++) {
					dev->channels[j].iq_in[2*(dev->waveend+job)] = ptr[dev->bins[j]].re;
					dev->channels[j].iq_in[2*(dev->waveend+job)+1] = ptr[dev->bins[j]].im;
					ptr += fft->step;
				}
			}
		}
#else
		for (int j = 0; j < dev->channel_count; j++) {
			dev->channels[j].wavein[dev->waveend] =
			sqrtf(fftout[dev->bins[j]][0] * fftout[dev->bins[j]][0] + fftout[dev->bins[j]][1] * fftout[dev->bins[j]][1]);
			if(dev->channels[j].needs_raw_iq) {
				dev->channels[j].iq_in[2*dev->waveend] = fftout[dev->bins[j]][0];
				dev->channels[j].iq_in[2*dev->waveend+1] = fftout[dev->bins[j]][1];
			}
		}
#endif // USE_BCM_VC

		dev->waveend += FFT_BATCH;

		if (dev->waveend >= WAVE_BATCH + AGC_EXTRA) {
			for (int i = 0; i < dev->channel_count; i++) {
				AFC afc(dev, i);
				channel_t* channel = dev->channels + i;
				freq_t *fparms = channel->freqlist + channel->freq_idx;
#if defined (__arm__) || defined (__aarch64__)
				float agcmin2 = fparms->agcmin * 4.5f;
				for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j++) {
					channel->waveref[j] = min(channel->wavein[j], agcmin2);
				}
#else
				__m128 agccap = _mm_set1_ps(fparms->agcmin * 4.5f);
				for (int j = 0; j < WAVE_BATCH + AGC_EXTRA; j += 4) {
					__m128 t = _mm_loadu_ps(channel->wavein + j);
					_mm_storeu_ps(channel->waveref + j, _mm_min_ps(t, agccap));
				}
#endif
				for (int j = AGC_EXTRA; j < WAVE_BATCH + AGC_EXTRA; j++) {
					// auto noise floor
					if (fparms->sqlevel < 0 && j % 16 == 0) {
						fparms->agcmin = fparms->agcmin * 0.97f + min(fparms->agcavgslow, fparms->agcmin) * 0.03f + 0.0001f;
					}

					// average power
					fparms->agcavgslow = fparms->agcavgslow * 0.99f + channel->waveref[j] * 0.01f;

					float sqlevel = fparms->sqlevel >= 0 ? (float)fparms->sqlevel : 3.0f * fparms->agcmin;
					if (channel->agcsq > 0) {
						channel->agcsq = max(channel->agcsq - 1, 1);
						if (channel->agcsq == 1 && fparms->agcavgslow > sqlevel) {
							channel->agcsq = -AGC_EXTRA * 2;
							channel->axcindicate = SIGNAL;
							if(channel->modulation == MOD_AM) {
							// fade in
								for (int k = j - AGC_EXTRA; k < j; k++) {
									if (channel->wavein[k] > sqlevel) {
										fparms->agcavgfast = fparms->agcavgfast * 0.9f + channel->wavein[k] * 0.1f;
									}
								}
							}
						}
					} else {
						if (channel->wavein[j] > sqlevel) {
							if(channel->modulation == MOD_AM)
								fparms->agcavgfast = fparms->agcavgfast * 0.995f + channel->wavein[j] * 0.005f;
							fparms->agclow = 0;
						} else {
							fparms->agclow++;
						}
						channel->agcsq = min(channel->agcsq + 1, -1);
						if ((channel->agcsq == -1 && fparms->agcavgslow < sqlevel) || fparms->agclow == AGC_EXTRA - 12) {
							channel->agcsq = AGC_EXTRA * 2;
							channel->axcindicate = NO_SIGNAL;
							if(channel->modulation == MOD_AM) {
								// fade out
								for (int k = j - AGC_EXTRA + 1; k < j; k++) {
									channel->waveout[k] = channel->waveout[k - 1] * 0.94f;
								}
							}
						}
					}

					bool signal_filtered = false;
					if(channel->agcsq < 0 && channel->needs_raw_iq) {
						// remove phase rotation introduced by FFT sliding window
						float swf, cwf, re, im;
						sincosf_lut(channel->dm_phi, &swf, &cwf);
						multiply(channel->iq_in[2*(j - AGC_EXTRA)], channel->iq_in[2*(j - AGC_EXTRA)+1], cwf, -swf, &re, &im);
						channel->dm_phi += channel->dm_dphi;
						channel->dm_phi &= 0xffffff;

						// apply lowpass filter, will be a no-op if not configured
						fparms->lowpass_filter.apply(re, im);

						// update I/Q and wave
						channel->iq_in[2*(j - AGC_EXTRA)] = re;
						channel->iq_in[2*(j - AGC_EXTRA)+1] = im;
						channel->wavein[j] = sqrt(re * re + im * im);

						if(fparms->lowpass_filter.enabled()) {
							fparms->filter_avg = fparms->filter_avg * 0.999f + channel->wavein[j] * 0.001f;

							if (fparms->filter_avg < sqlevel) {
								signal_filtered = true;
								channel->axcindicate = NO_SIGNAL;
							} else {
								channel->axcindicate = SIGNAL;
							}
						}
					}
					if(channel->agcsq != -1 || signal_filtered) {
						channel->waveout[j] = 0;
						if(channel->has_iq_outputs) {
							channel->iq_out[2*(j - AGC_EXTRA)] = 0;
							channel->iq_out[2*(j - AGC_EXTRA)+1] = 0;
						}
					} else {
						const float &re = channel->iq_in[2*(j - AGC_EXTRA)];
						const float &im = channel->iq_in[2*(j - AGC_EXTRA)+1];

						if(channel->has_iq_outputs) {
							channel->iq_out[2*(j - AGC_EXTRA)] = re;
							channel->iq_out[2*(j - AGC_EXTRA)+1] = im;
						}

						if(channel->modulation == MOD_AM) {
							channel->waveout[j] = (channel->wavein[j - AGC_EXTRA] - fparms->agcavgfast) / (fparms->agcavgfast * 1.5f);
							if (abs(channel->waveout[j]) > 0.8f) {
								channel->waveout[j] *= 0.85f;
								fparms->agcavgfast *= 1.15f;
							}
						}
#ifdef NFM
						else if(channel->modulation == MOD_NFM) {
// FM demod
							if(fm_demod == FM_FAST_ATAN2) {
								channel->waveout[j] = polar_disc_fast(re, im, channel->pr, channel->pj);
							} else if(fm_demod == FM_QUADRI_DEMOD) {
								channel->waveout[j] = fm_quadri_demod(re, im, channel->pr, channel->pj);
							}
							channel->pr = re;
							channel->pj = im;
// de-emphasis IIR + DC blocking
							fparms->agcavgfast = fparms->agcavgfast * 0.995f + channel->waveout[j] * 0.005f;
							channel->waveout[j] -= fparms->agcavgfast;
							channel->waveout[j] = channel->waveout[j] * (1.0f - channel->alpha) + channel->waveout[j-1] * channel->alpha;
						}
#endif // NFM

// apply the notch filter.  If no filter configured, this will no-op
						fparms->notch_filter.apply(channel->waveout[j]);
					}
				}
				memmove(channel->wavein, channel->wavein + WAVE_BATCH, (dev->waveend - WAVE_BATCH) * sizeof(float));
				if(channel->needs_raw_iq) {
					memmove(channel->iq_in, channel->iq_in + 2 * WAVE_BATCH, (dev->waveend - WAVE_BATCH) * sizeof(float) * 2);
				}

#ifdef USE_BCM_VC
				afc.finalize(dev, i, fft->out);
#else
				afc.finalize(dev, i, demod_params->fftout);
#endif

				if (tui) {
					if(dev->mode == R_SCAN) {
						GOTOXY(0, device_num * 17 + dev->row + 3);
						printf("%4.0f/%3.0f%c %7.3f ",
							fparms->agcavgslow,
							(fparms->sqlevel >= 0 ? fparms->sqlevel : fparms->agcmin),
							channel->axcindicate,
							(dev->channels[0].freqlist[channel->freq_idx].frequency / 1000000.0));
					} else {
						GOTOXY(i*10, device_num * 17 + dev->row + 3);
						printf("%4.0f/%3.0f%c ",
							fparms->agcavgslow,
							(fparms->sqlevel >= 0 ? fparms->sqlevel : fparms->agcmin),
							channel->axcindicate);
					}
					fflush(stdout);
				}

				if (channel->axcindicate != NO_SIGNAL) {
					channel->freqlist[channel->freq_idx].active_counter++;
				}
			}
			dev->waveavail = 1;
			dev->waveend -= WAVE_BATCH;
			if(DEBUG) {
				gettimeofday(&te, NULL);
				debug_bulk_print("waveavail %lu.%lu %lu\n", te.tv_sec, te.tv_usec, (te.tv_sec - ts.tv_sec) * 1000000UL + te.tv_usec - ts.tv_usec);
				ts.tv_sec = te.tv_sec;
				ts.tv_usec = te.tv_usec;
			}
			safe_cond_signal(&mp3_cond, &mp3_mutex);
			dev->row++;
			if (dev->row == 12) {
				dev->row = 0;
			}
		}

		dev->input->bufs = (dev->input->bufs + bps * FFT_BATCH) % dev->input->buf_size;
		device_num = next_device(demod_params, device_num);
	}
}

void usage() {
	cout<<"Usage: rtl_airband [options] [-c <config_file_path>]\n\
\t-h\t\t\tDisplay this help text\n\
\t-f\t\t\tRun in foreground, display textual waterfalls\n\
\t-F\t\t\tRun in foreground, do not display waterfalls (for running as a systemd service)\n";
#ifdef NFM
	cout<<"\t-Q\t\t\tUse quadri correlator for FM demodulation (default is atan2)\n";
#endif
#if DEBUG
	cout<<"\t-d <file>\t\tLog debugging information to <file> (default is "<<DEBUG_PATH<<")\n";
#endif
	cout<<"\t-e\t\t\tPrint messages to standard error (disables syslog logging)\n";
	cout<<"\t-c <config_file_path>\tUse non-default configuration file\n\t\t\t\t(default: "<<CFGFILE<<")\n\
\t-v\t\t\tDisplay version and exit\n";
	exit(EXIT_SUCCESS);
}

static int count_devices_running() {
	int ret = 0;
	for(int i = 0; i < device_count; i++) {
		if(devices[i].input->state == INPUT_RUNNING) {
			ret++;
		}
	}
	return ret;
}

int main(int argc, char* argv[]) {
#ifdef WITH_PROFILING
	ProfilerStart("rtl_airband.prof");
#endif
#pragma GCC diagnostic ignored "-Wwrite-strings"
	char *cfgfile = CFGFILE;
	char *pidfile = PIDFILE;
#pragma GCC diagnostic warning "-Wwrite-strings"
	int opt;
	char optstring[16] = "efFhvc:";

#ifdef NFM
	strcat(optstring, "Q");
#endif
#if DEBUG
	strcat(optstring, "d:");
#endif

	while((opt = getopt(argc, argv, optstring)) != -1) {
		switch(opt) {
#ifdef NFM
			case 'Q':
				fm_demod = FM_QUADRI_DEMOD;
				break;
#endif
#if DEBUG
			case 'd':
				debug_path = strdup(optarg);
				break;
#endif
			case 'e':
				do_syslog = 0;
				break;
			case 'f':
				foreground = 1;
				tui = 1;
				break;
			case 'F':
				foreground = 1;
				tui = 0;
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
#if DEBUG
	if(!debug_path) debug_path = strdup(DEBUG_PATH);
	init_debug(debug_path);
#endif
#if !defined (__arm__) && !defined (__aarch64__)
#define cpuid(func,ax,bx,cx,dx)\
	__asm__ __volatile__ ("cpuid":\
		"=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));
	int a,b,c,d;
	cpuid(1,a,b,c,d);
	if((int)((d >> 25) & 0x1)) {
		/* NOOP */
	} else {
		printf("Unsupported CPU.\n");
		error();
	}
#endif /* !__arm__ */

	// If executing other than as root, GPU memory gets alloc'd and the
	// 'permission denied' message on /dev/mem kills rtl_airband without
	// releasing GPU memory.
#ifdef USE_BCM_VC
	// XXX should probably do this check in other circumstances also.
	if(0 != getuid()) {
		cerr<<"FFT library requires that rtl_airband be executed as root\n";
		exit(1);
	}
#endif

	// read config
	try {
		Config config;
		config.readFile(cfgfile);
		Setting &root = config.getRoot();
		if(root.exists("pidfile")) pidfile = strdup(root["pidfile"]);
		if(root.exists("fft_size")) {
			int fsize = (int)(root["fft_size"]);
			fft_size_log = 0;
			for(size_t i = MIN_FFT_SIZE_LOG; i <= MAX_FFT_SIZE_LOG; i++) {
				if(fsize == 1 << i) {
					fft_size = (size_t)fsize;
					fft_size_log = i;
					break;
				}
			}
			if(fft_size_log == 0) {
				cerr<<"Configuration error: invalid fft_size value (must be a power of two in range "<<
					(1<<MIN_FFT_SIZE_LOG)<<"-"<<(1<<MAX_FFT_SIZE_LOG)<<")\n";
				error();
			}
		}
		if(root.exists("shout_metadata_delay")) shout_metadata_delay = (int)(root["shout_metadata_delay"]);
		if(shout_metadata_delay < 0 || shout_metadata_delay > 2*TAG_QUEUE_LEN) {
			cerr<<"Configuration error: shout_metadata_delay is out of allowed range (0-"<<2 * TAG_QUEUE_LEN<<")\n";
			error();
		}
		if(root.exists("localtime") && (bool)root["localtime"] == true)
			use_localtime = true;
		if(root.exists("multiple_demod_threads") && (bool)root["multiple_demod_threads"] == true) {
#ifdef USE_BCM_VC
			cerr<<"Using multiple_demod_threads not supported with BCM VideoCore for FFT\n";
			exit(1);
#endif
			multiple_demod_threads = true;
		}
		if(root.exists("log_scan_activity") && (bool)root["log_scan_activity"] == true)
			log_scan_activity = true;
		if(root.exists("stats_filepath"))
			stats_filepath = strdup(root["stats_filepath"]);
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

#ifndef __MINGW32__
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

		devices = (device_t *)XCALLOC(device_count, sizeof(device_t));
		shout_init();
		if(do_syslog) openlog("rtl_airband", LOG_PID, LOG_DAEMON);

		if(root.exists("mixers")) {
			Setting &mx = config.lookup("mixers");
			mixers = (mixer_t *)XCALLOC(mx.getLength(), sizeof(struct mixer_t));
			if((mixer_count = parse_mixers(mx)) > 0) {
				mixers = (mixer_t *)XREALLOC(mixers, mixer_count * sizeof(struct mixer_t));
			} else {
				free(mixers);
			}
		} else {
			mixer_count = 0;
		}

		uint32_t devs_enabled = parse_devices(devs);
		if (devs_enabled < 1) {
			cerr<<"Configuration error: no devices defined\n";
			error();
		}
		device_count = devs_enabled;
		debug_print("mixer_count=%d\n", mixer_count);
		for(int z = 0; z < mixer_count; z++) {
			mixer_t *m = &mixers[z];
			debug_print("mixer[%d]: name=%s, input_count=%d, output_count=%d\n", z, m->name, m->input_count, m->channel.output_count);
		}
	} catch(const FileIOException &e) {
			cerr<<"Cannot read configuration file "<<cfgfile<<"\n";
			error();
	} catch(const ParseException &e) {
			cerr<<"Error while parsing configuration file "<<cfgfile<<" line "<<e.getLine()<<": "<<e.getError()<<"\n";
			error();
	} catch(const SettingNotFoundException &e) {
			cerr<<"Configuration error: mandatory parameter missing: "<<e.getPath()<<"\n";
			error();
	} catch(const SettingTypeException &e) {
			cerr<<"Configuration error: invalid parameter type: "<<e.getPath()<<"\n";
			error();
	} catch(const ConfigException &e) {
			cerr<<"Unhandled config exception\n";
			error();
	}

	log(LOG_INFO, "RTLSDR-Airband version %s starting\n", RTL_AIRBAND_VERSION);

#ifndef __MINGW32__ // Fork Were Nowhere Near the Windows
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
					log(LOG_WARNING, "Cannot write pidfile: %s\n", strerror(errno));
				} else {
					fprintf(f, "%ld\n", (long)getpid());
					fclose(f);
				}
			}
		}
	}
#endif

	for (int i = 0; i < mixer_count; i++) {
		if(mixers[i].enabled == false)
			continue;		// no inputs connected = no need to initialize output
		channel_t *channel = &mixers[i].channel;
		if(channel->need_mp3)
			channel->lame = airlame_init(mixers[i].channel.mode, mixers[i].channel.highpass, mixers[i].channel.lowpass);
		for (int k = 0; k < channel->output_count; k++) {
			output_t *output = channel->outputs + k;
			if(output->type == O_ICECAST) {
				shout_setup((icecast_data *)(output->data), channel->mode);
#ifdef PULSE
			} else if(output->type == O_PULSE) {
				pulse_init();
				pulse_setup((pulse_data *)(output->data), channel->mode);
#endif
			}
		}
	}
	for (int i = 0; i < device_count; i++) {
		device_t* dev = devices + i;
		for (int j = 0; j < dev->channel_count; j++) {
			channel_t* channel = dev->channels + j;

			// If the channel has icecast or MP3 file output, we will attempt to
			// initialize a separate LAME context for MP3 encoding.
			if(channel->need_mp3)
				channel->lame = airlame_init(channel->mode, channel->highpass, channel->lowpass);

			for (int k = 0; k < channel->output_count; k++) {
				output_t *output = channel->outputs + k;
				if(output->type == O_ICECAST) {
					shout_setup((icecast_data *)(output->data), channel->mode);
#ifdef PULSE
				} else if(output->type == O_PULSE) {
					pulse_init();
					pulse_setup((pulse_data *)(output->data), channel->mode);
#endif
				}
			}
		}
		if(input_init(dev->input) != 0 || dev->input->state != INPUT_INITIALIZED) {
			if(errno != 0) {
				cerr<<"Failed to initialize input device "<<i<<": "<<strerror(errno)<<" - aborting\n";
			} else {
				cerr<<"Failed to initialize input device "<<i<<" - aborting\n";
			}
			error();
		}
		if(input_start(dev->input) != 0) {
			cerr<<"Failed to start input on device "<<i<<": "<<strerror(errno)<<" - aborting\n";
			error();
		}
		if(dev->mode == R_SCAN) {
// FIXME: set errno
			if(pthread_mutex_init(&dev->tag_queue_lock, NULL) != 0) {
				cerr<<"Failed to initialize mutex - aborting\n";
				error();
			}
// FIXME: not needed when freq_count == 1?
			pthread_create(&dev->controller_thread, NULL, &controller_thread, dev);
		}
	}

	int timeout = 50;		// 5 seconds
	while ((devices_running = count_devices_running()) != device_count && timeout > 0) {
		SLEEP(100);
		timeout--;
	}
	if((devices_running = count_devices_running()) != device_count) {
		log(LOG_ERR, "%d device(s) failed to initialize - aborting\n", device_count - devices_running);
		error();
	}
	if (tui) {
		printf("\e[1;1H\e[2J");

		GOTOXY(0, 0);
		printf("                                                                               ");
		for (int i = 0; i < device_count; i++) {
			GOTOXY(0, i * 17 + 1);
			for (int j = 0; j < devices[i].channel_count; j++) {
				printf(" %7.3f  ", devices[i].channels[j].freqlist[devices[i].channels[j].freq_idx].frequency / 1000000.0);
			}
			if (i != device_count - 1) {
				GOTOXY(0, i * 17 + 16);
				printf("-------------------------------------------------------------------------------");
			}
		}
	}
	THREAD thread2;
	pthread_create(&thread2, NULL, &output_check_thread, NULL);
	THREAD thread3;
	pthread_create(&thread3, NULL, &output_thread, NULL);
	THREAD thread4;
	if(mixer_count > 0)
		pthread_create(&thread4, NULL, &mixer_thread, NULL);
#ifdef PULSE
	pulse_start();
#endif
	sincosf_lut_init();

	if (!multiple_demod_threads) {
		demod_params_t params;
		init_demod(&params, 0, device_count - 1);
		demodulate(&params);
	} else {
		demod_params_t *params = (demod_params_t *)XCALLOC(device_count, sizeof(demod_params_t));
		THREAD *threads = (THREAD *)XCALLOC(device_count, sizeof(THREAD));
		for (int i = 0; i < device_count; i++) {
			init_demod(&params[i], i, i);
		}
		for (int i = 0; i < device_count; i++) {
			pthread_create(&threads[i], NULL, &demodulate, &params[i]);
		}
		for (int i = 0; i < device_count; i++) {
			pthread_join(threads[i], NULL);
		}
	}

	log(LOG_INFO, "Cleaning up\n");
	for (int i = 0; i < device_count; i++) {
		if(devices[i].mode == R_SCAN)
			pthread_join(devices[i].controller_thread, NULL);
		if(input_stop(devices[i].input) != 0 || devices[i].input->state != INPUT_STOPPED) {
			if(errno != 0) {
				log(LOG_ERR, "Failed do stop device #%d: %s\n", i, strerror(errno));
			} else {
				log(LOG_ERR, "Failed do stop device #%d\n", i);
			}
		}
	}
	log(LOG_INFO, "Input threads closed\n");

	for (int i = 0; i < device_count; i++) {
		device_t* dev = devices + i;
		disable_device_outputs(dev);

		for (int j = 0; j < dev->channel_count; j++) {
			channel_t* channel = dev->channels + j;
			if(channel->need_mp3 && channel->lame)
				lame_close(channel->lame);
		}
	}

	close_debug();
#ifdef WITH_PROFILING
	ProfilerStop();
#endif
	return 0;
}

// Default constructor is no filter
NotchFilter::NotchFilter(void) : enabled_(false) {
}

// Notch Filter based on https://www.dsprelated.com/showcode/173.php
NotchFilter::NotchFilter(float notch_freq, float sample_freq, float q): enabled_(true), x{0.0}, y{0.0} {
	if (notch_freq <= 0.0) {
		debug_print("Invalid frequency %f Hz, disabling notch filter\n", notch_freq);
		enabled_ = false;
		return;
	}

	debug_print("Adding notch filter for %f Hz with parameters {%f, %f}\n", notch_freq, sample_freq, q);

	float wo = 2*M_PI*(notch_freq/sample_freq);

	e = 1/(1 + tan(wo/(q*2)));
	p = cos(wo);
	d[0] = e;
	d[1] = 2*e*p;
	d[2] = (2*e-1);

	debug_print("wo:%f e:%f p:%f d:{%f,%f,%f}\n", wo, e, p, d[0], d[1], d[2]);
}

void NotchFilter::apply(float &value) {
	if (!enabled_) {
		return;
	}

	x[0] = x[1];
	x[1] = x[2];
	x[2] = value;

	y[0] = y[1];
	y[1] = y[2];
	y[2] = d[0]*x[2] - d[1]*x[1] + d[0]*x[0] + d[1]*y[1] - d[2]*y[0];

	value = y[2];
}

// Default constructor is no filter
LowpassFilter::LowpassFilter(void) : enabled_(false) {
}

// 2nd order lowpass Bessel filter, based entirely on a simplification of https://www-users.cs.york.ac.uk/~fisher/mkfilter/
LowpassFilter::LowpassFilter(float freq, float sample_freq) : enabled_(true) {
	if (freq <= 0.0) {
		debug_print("Invalid frequency %f Hz, disabling lowpass filter\n", freq);
		enabled_ = false;
		return;
	}

	debug_print("Adding lowpass filter at %f Hz with a sample rate of %f\n", freq, sample_freq);

	double raw_alpha = (double)freq/sample_freq;
	double warped_alpha = tan(M_PI * raw_alpha) / M_PI;

	complex<double> zeros[2] = {-1.0, -1.0};
	complex<double> poles[2];
	poles[0] = blt(M_PI * 2 * warped_alpha * complex<double>(-1.10160133059e+00, 6.36009824757e-01));
	poles[1] = blt(M_PI * 2 * warped_alpha * conj(complex<double>(-1.10160133059e+00, 6.36009824757e-01)));

	complex<double> topcoeffs[3];
	complex<double> botcoeffs[3];
	expand(zeros, 2, topcoeffs);
	expand(poles, 2, botcoeffs);
	complex<double> gain_complex = evaluate(topcoeffs, 2, botcoeffs, 2, 1.0);
	gain = hypot(gain_complex.imag(), gain_complex.real());

	for (int i = 0; i <= 2; i++)
	{
		ycoeffs[i] = -(botcoeffs[i].real() / botcoeffs[2].real());
	}

	debug_print("gain: %f, ycoeffs: {%f, %f}\n", gain, ycoeffs[0], ycoeffs[1]);
}

complex<double> LowpassFilter::blt(complex<double> pz)
{
	return (2.0 + pz) / (2.0 - pz);
}

/* evaluate response, substituting for z */
complex<double> LowpassFilter::evaluate(complex<double> topco[], int nz, complex<double> botco[], int np, complex<double> z)
{
	return eval(topco, nz, z) / eval(botco, np, z);
}

/* evaluate polynomial in z, substituting for z */
complex<double> LowpassFilter::eval(complex<double> coeffs[], int npz, complex<double> z)
{
	complex<double> sum (0.0);
	for (int i = npz; i >= 0; i--) {
		sum = (sum * z) + coeffs[i];
	}
	return sum;
}

/* compute product of poles or zeros as a polynomial of z */
void LowpassFilter::expand(complex<double> pz[], int npz, complex<double> coeffs[])
{
	coeffs[0] = 1.0;
	for (int i = 0; i < npz; i++)
	{
		coeffs[i+1] = 0.0;
	}
	for (int i = 0; i < npz; i++)
	{
		multin(pz[i], npz, coeffs);
	}
	/* check computed coeffs of z^k are all real */
	for (int i = 0; i < npz+1; i++)
	{
		if (fabs(coeffs[i].imag()) > 1e-10)
		{
			log(LOG_ERR, "coeff of z^%d is not real; poles/zeros are not complex conjugates\n", i);
			error();
		}
	}
}

void LowpassFilter::multin(complex<double> w, int npz, complex<double> coeffs[])
{
	/* multiply factor (z-w) into coeffs */
	complex<double> nw = -w;
	for (int i = npz; i >= 1; i--)
	{
		coeffs[i] = (nw * coeffs[i]) + coeffs[i-1];
	}
	coeffs[0] = nw * coeffs[0];
}

void LowpassFilter::apply(float &r, float &j) {
	if (!enabled_) {
		return;
	}

	complex<float> input(r, j);

	xv[0] = xv[1];
	xv[1] = xv[2];
	xv[2] = input / gain;

	yv[0] = yv[1];
	yv[1] = yv[2];
	yv[2] = (xv[0] + xv[2]) + (2.0f * xv[1]) + (ycoeffs[0] * yv[0]) + (ycoeffs[1] * yv[1]);

	r = yv[2].real();
	j = yv[2].imag();
}

// vim: ts=4
