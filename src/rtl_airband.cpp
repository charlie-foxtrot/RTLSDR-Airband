/*
 * RTLSDR AM/NFM demodulator, mixer, streamer and recorder
 *
 * Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
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

#include "config.h"

#if defined WITH_BCM_VC && !defined __arm__
#error Broadcom VideoCore support can only be enabled on ARM builds
#endif

// From this point we may safely assume that WITH_BCM_VC implies __arm__

#if defined (__arm__) || defined (__aarch64__)

#ifdef WITH_BCM_VC
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
int devices_running = 0;
int foreground = 0;			// daemonize
int tui = 0;				// do not display textual user interface
int do_syslog = 1;
int shout_metadata_delay = 3;
volatile int do_exit = 0;
bool use_localtime = false;
bool multiple_demod_threads = false;
bool multiple_output_threads = false;
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

#ifdef WITH_BCM_VC
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

void init_demod(demod_params_t *params, Signal *signal, int device_start, int device_end) {
	assert(params != NULL);
	assert(signal != NULL);

	params->mp3_signal = signal;
	params->device_start = device_start;
	params->device_end = device_end;

#ifndef WITH_BCM_VC
	params->fftin = fftwf_alloc_complex(fft_size);
	params->fftout = fftwf_alloc_complex(fft_size);
	params->fft = fftwf_plan_dft_1d(fft_size, params->fftin, params->fftout, FFTW_FORWARD, FFTW_MEASURE);
#endif
}

void init_output(output_params_t *params, int device_start, int device_end, int mixer_start, int mixer_end) {
	assert(params != NULL);

	params->mp3_signal = new Signal;
	params->device_start = device_start;
	params->device_end = device_end;
	params->mixer_start = mixer_start;
	params->mixer_end = mixer_end;
}

int next_device(demod_params_t *params, int current) {
	current++;
	if (current < params->device_end) {
		return current;
	}
	return params->device_start;
}

void *demodulate(void *params) {
	assert(params != NULL);
	demod_params_t *demod_params = (demod_params_t *) params;

	debug_print("Starting demod thread, devices %d:%d, signal %p\n", demod_params->device_start, demod_params->device_end, demod_params->mp3_signal);

	// initialize fft engine
#ifdef WITH_BCM_VC
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
	float ALIGNED32 levels_u8[256], levels_s8[256];
	float *levels_ptr = NULL;

	for (int i=0; i<256; i++) {
		levels_u8[i] = (i-127.5f)/127.5f;
	}
	for (int16_t i=-127; i<128; i++) {
		levels_s8[(uint8_t)i] = i/128.0f;
	}

	// initialize fft window
	// blackman 7
	// the whole matrix is computed
#ifdef WITH_BCM_VC
	float ALIGNED32 window[fft_size * 2];
#else
	float ALIGNED32 window[fft_size];
#endif
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
#ifdef WITH_BCM_VC
		window[i * 2] = window[i * 2 + 1] = (float)x;
#else
		window[i] = (float)x;
#endif
	}

#ifdef DEBUG
	struct timeval ts, te;
	gettimeofday(&ts, NULL);
#endif
	size_t available;
	int device_num = demod_params->device_start;
	while (true) {

		if(do_exit) {
#ifdef WITH_BCM_VC
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
			float const scale = 1.0f / dev->input->fullscale;
#ifdef WITH_BCM_VC
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
				 fftin[i][0] = scale * (float)buf2[0] * window[i];
				 fftin[i][1] = scale * (float)buf2[1] * window[i];
			}
#endif
		} else if(dev->input->sfmt == SFMT_F32) {
			float const scale = 1.0f / dev->input->fullscale;
#ifdef WITH_BCM_VC
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
				fftin[i][0] = scale * buf2[0] * window[i];
				fftin[i][1] = scale * buf2[1] * window[i];
			}
#endif
		} else {	// S8 or U8
			levels_ptr = (dev->input->sfmt == SFMT_U8 ? levels_u8 : levels_s8);
#ifdef WITH_BCM_VC
			sample_fft_arg sfa = {fft_size / 4, fft->in};
			for (size_t i = 0; i < FFT_BATCH; i++) {
				samplefft(&sfa, dev->input->buffer + dev->input->bufs + i * bps, window, levels_ptr);
				sfa.dest+= fft->step;
			}
#elif defined (__arm__) || defined (__aarch64__)
			unsigned char* buf2 = dev->input->buffer + dev->input->bufs;
			for (size_t i = 0; i < fft_size; i++, buf2 += 2) {
				fftin[i][0] = levels_ptr[buf2[0]] * window[i];
				fftin[i][1] = levels_ptr[buf2[1]] * window[i];
			}
#else /* x86 */
			unsigned char* buf2 = dev->input->buffer + dev->input->bufs;
			for (size_t i = 0; i < fft_size; i += 2, buf2 += 4) {
				__m128 a = _mm_set_ps(levels_ptr[buf2[3]], levels_ptr[buf2[2]], levels_ptr[buf2[1]], levels_ptr[buf2[0]]);
				__m128 b = _mm_set_ps(window[i+1], window[i+1], window[i], window[i]);
				a = _mm_mul_ps(a, b);
				_mm_store_ps(&fftin[i][0], a);
			}
#endif
		}
#ifdef WITH_BCM_VC
		gpu_fft_execute(fft);
#else
		fftwf_execute(demod_params->fft);
#endif

#ifdef WITH_BCM_VC
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
#endif // WITH_BCM_VC

		dev->waveend += FFT_BATCH;

		if (dev->waveend >= WAVE_BATCH + AGC_EXTRA) {
			for (int i = 0; i < dev->channel_count; i++) {
				AFC afc(dev, i);
				channel_t* channel = dev->channels + i;
				freq_t *fparms = channel->freqlist + channel->freq_idx;

				// set to NO_SIGNAL, will be updated to SIGNAL based on squelch below
				channel->axcindicate = NO_SIGNAL;

				for (int j = AGC_EXTRA; j < WAVE_BATCH + AGC_EXTRA; j++) {

					float &real = channel->iq_in[2*(j - AGC_EXTRA)];
					float &imag = channel->iq_in[2*(j - AGC_EXTRA)+1];

					fparms->squelch.process_raw_sample(channel->wavein[j]);

					// If squelch is open / opening and using I/Q, then cleanup the signal and possibly update squelch.
					if (fparms->squelch.should_filter_sample() && channel->needs_raw_iq) {

						// remove phase rotation introduced by FFT sliding window
						float swf, cwf, re_tmp, im_tmp;
						sincosf_lut(channel->dm_phi, &swf, &cwf);
						multiply(real, imag, cwf, -swf, &re_tmp, &im_tmp);
						channel->dm_phi += channel->dm_dphi;
						channel->dm_phi &= 0xffffff;

						// apply lowpass filter, will be a no-op if not configured
						fparms->lowpass_filter.apply(re_tmp, im_tmp);

						// update I/Q and wave
						real = re_tmp;
						imag = im_tmp;
						channel->wavein[j] = sqrt(real * real + imag * imag);

						// update squelch post-cleanup
						if (fparms->lowpass_filter.enabled()) {
							fparms->squelch.process_filtered_sample(channel->wavein[j]);
						}
					}

					if(fparms->modulation == MOD_AM) {
						// if squelch is just opening then bootstrip agcavgfast with prior values of wavein
						if (fparms->squelch.first_open_sample()) {
							for (int k = j - AGC_EXTRA; k < j; k++) {
								if (channel->wavein[k] >= fparms->squelch.squelch_level()) {
									fparms->agcavgfast = fparms->agcavgfast * 0.9f + channel->wavein[k] * 0.1f;
								}
							}
						}
						// if squelch is just closing then fade out the prior samples of waveout
						else if (fparms->squelch.last_open_sample()) {
							for (int k = j - AGC_EXTRA + 1; k < j; k++) {
								channel->waveout[k] = channel->waveout[k - 1] * 0.94f;
							}
						}
					}

					// If squelch sees power then do modulation-specific processing
					float waveout = channel->waveout[j];
					float agcavgfast = fparms->agcavgfast;
					
					if (fparms->squelch.should_process_audio()) {
						if(fparms->modulation == MOD_AM) {

							if( channel->wavein[j] > fparms->squelch.squelch_level() ) {
								agcavgfast = agcavgfast * 0.995f + channel->wavein[j] * 0.005f;
							}

							waveout = (channel->wavein[j - AGC_EXTRA] - agcavgfast) / (agcavgfast * 1.5f);
							if (abs(waveout) > 0.8f) {
								waveout *= 0.85f;
								agcavgfast *= 1.15f;
							}
						}
#ifdef NFM
						else if(fparms->modulation == MOD_NFM) {
							// FM demod
							if(fm_demod == FM_FAST_ATAN2) {
								waveout = polar_disc_fast(real, imag, channel->pr, channel->pj);
							} else if(fm_demod == FM_QUADRI_DEMOD) {
								waveout = fm_quadri_demod(real, imag, channel->pr, channel->pj);
							}
							channel->pr = real;
							channel->pj = imag;

							// de-emphasis IIR + DC blocking
							agcavgfast = agcavgfast * 0.995f + waveout * 0.005f;
							waveout -= agcavgfast;
							waveout = waveout * (1.0f - channel->alpha) + channel->waveout[j-1] * channel->alpha;
						}
#endif // NFM
						
						// process audio sample for CTCSS, will be no-op if not configured
						fparms->squelch.process_audio_sample(waveout);
					}
					
					// If squelch is still open then save samples to output
					if (fparms->squelch.is_open()) {

						// save the I/Q samples
						if(channel->has_iq_outputs) {
							channel->iq_out[2*(j - AGC_EXTRA)] = real;
							channel->iq_out[2*(j - AGC_EXTRA)+1] = imag;
						}
						
						// save the waveout and update agc
						channel->waveout[j] = waveout;
						fparms->agcavgfast = agcavgfast;

						// apply the notch filter, will be a no-op if not configured
						fparms->notch_filter.apply(channel->waveout[j]);

						// sanitize the output to prevent assertion failure in libmp3lame on NaN or infinity
						if(!isnormal(channel->waveout[j])) {
							channel->waveout[j] = 0.f;
						}

						channel->axcindicate = SIGNAL;

					// Squelch is closed
					} else {
						channel->waveout[j] = 0;
						if(channel->has_iq_outputs) {
							channel->iq_out[2*(j - AGC_EXTRA)] = 0;
							channel->iq_out[2*(j - AGC_EXTRA)+1] = 0;
						}
					}
				}
				memmove(channel->wavein, channel->wavein + WAVE_BATCH, (dev->waveend - WAVE_BATCH) * sizeof(float));
				if(channel->needs_raw_iq) {
					memmove(channel->iq_in, channel->iq_in + 2 * WAVE_BATCH, (dev->waveend - WAVE_BATCH) * sizeof(float) * 2);
				}

#ifdef WITH_BCM_VC
				afc.finalize(dev, i, fft->out);
#else
				afc.finalize(dev, i, demod_params->fftout);
#endif

				if (tui) {
					char symbol = fparms->squelch.signal_outside_filter() ? '~' : (char)channel->axcindicate;
					if(dev->mode == R_SCAN) {
						GOTOXY(0, device_num * 17 + dev->row + 3);
						printf("%4.0f/%3.0f%c %7.3f ",
							level_to_dBFS(fparms->squelch.signal_level()),
							level_to_dBFS(fparms->squelch.noise_level()),
							symbol,
							(dev->channels[0].freqlist[channel->freq_idx].frequency / 1000000.0));
					} else {
						GOTOXY(i*10, device_num * 17 + dev->row + 3);
						printf("%4.0f/%3.0f%c ",
							level_to_dBFS(fparms->squelch.signal_level()),
							level_to_dBFS(fparms->squelch.noise_level()),
							symbol);
					}
					fflush(stdout);
				}

				if (channel->axcindicate != NO_SIGNAL) {
					channel->freqlist[channel->freq_idx].active_counter++;
				}
			}
			if (dev->waveavail == 1) {
				debug_print("devices[%d]: output channel overrun\n", device_num);
				dev->output_overrun_count++;
			} else {
				dev->waveavail = 1;
			}
			dev->waveend -= WAVE_BATCH;
#ifdef DEBUG
			gettimeofday(&te, NULL);
			debug_bulk_print("waveavail %lu.%lu %lu\n", te.tv_sec, te.tv_usec, (te.tv_sec - ts.tv_sec) * 1000000UL + te.tv_usec - ts.tv_usec);
			ts.tv_sec = te.tv_sec;
			ts.tv_usec = te.tv_usec;
#endif
			demod_params->mp3_signal->send();
			dev->row++;
			if (dev->row == 12) {
				dev->row = 0;
			}
		}

		dev->input->bufs = (dev->input->bufs + bps * FFT_BATCH) % dev->input->buf_size;
		device_num = next_device(demod_params, device_num);
	}
}


