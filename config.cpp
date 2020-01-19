/*
 * config.cpp
 * Configuration parsing routines
 *
 * Copyright (c) 2015-2018 Tomasz Lemiech <szpajder@gmail.com>
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

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <assert.h>
#include <stdint.h>				// uint32_t
#include <syslog.h>
#include <libconfig.h++>
#include "input-common.h"			// input_t
#include "rtl_airband.h"

using namespace std;

static int parse_outputs(libconfig::Setting &outs, channel_t *channel, int i, int j, bool parsing_mixers) {
	int oo = 0;
	for(int o = 0; o < channel->output_count; o++) {
		if(outs[o].exists("disable") && (bool)outs[o]["disable"] == true) {
			continue;
		}
		if(!strncmp(outs[o]["type"], "icecast", 7)) {
			channel->outputs[oo].data = XCALLOC(1, sizeof(struct icecast_data));
			channel->outputs[oo].type = O_ICECAST;
			icecast_data *idata = (icecast_data *)(channel->outputs[oo].data);
			idata->hostname = strdup(outs[o]["server"]);
			idata->port = outs[o]["port"];
			idata->mountpoint = strdup(outs[o]["mountpoint"]);
			idata->username = strdup(outs[o]["username"]);
			idata->password = strdup(outs[o]["password"]);
			if(outs[o].exists("name"))
				idata->name = strdup(outs[o]["name"]);
			if(outs[o].exists("genre"))
				idata->genre = strdup(outs[o]["genre"]);
			if(outs[o].exists("description"))
				idata->description = strdup(outs[o]["description"]);
			if(outs[o].exists("send_scan_freq_tags"))
				idata->send_scan_freq_tags = (bool)outs[o]["send_scan_freq_tags"];
			else
				idata->send_scan_freq_tags = 0;
			channel->need_mp3 = 1;
		} else if(!strncmp(outs[o]["type"], "file", 4)) {
			channel->outputs[oo].data = XCALLOC(1, sizeof(struct file_data));
			channel->outputs[oo].type = O_FILE;
			file_data *fdata = (file_data *)(channel->outputs[oo].data);
			fdata->dir = strdup(outs[o]["directory"]);
			fdata->prefix = strdup(outs[o]["filename_template"]);
			fdata->continuous = outs[o].exists("continuous") ?
				(bool)(outs[o]["continuous"]) : false;
			fdata->append = (!outs[o].exists("append")) || (bool)(outs[o]["append"]);
			channel->need_mp3 = 1;
		} else if(!strncmp(outs[o]["type"], "rawfile", 7)) {
			if(parsing_mixers) {	// rawfile outputs not allowed for mixers
				cerr<<"Configuration error: mixers.["<<i<<"] outputs["<<o<<"]: rawfile output is not allowed for mixers\n";
				error();
			}
			channel->outputs[oo].data = XCALLOC(1, sizeof(struct file_data));
			channel->outputs[oo].type = O_RAWFILE;
			file_data *fdata = (file_data *)(channel->outputs[oo].data);
			fdata->dir = strdup(outs[o]["directory"]);
			fdata->prefix = strdup(outs[o]["filename_template"]);
			fdata->continuous = outs[o].exists("continuous") ?
				(bool)(outs[o]["continuous"]) : false;
			fdata->append = (!outs[o].exists("append")) || (bool)(outs[o]["append"]);
			channel->needs_raw_iq = channel->has_iq_outputs = 1;
		} else if(!strncmp(outs[o]["type"], "mixer", 5)) {
			if(parsing_mixers) {	// mixer outputs not allowed for mixers
				cerr<<"Configuration error: mixers.["<<i<<"] outputs["<<o<<"]: mixer output is not allowed for mixers\n";
				error();
			}
			channel->outputs[oo].data = XCALLOC(1, sizeof(struct mixer_data));
			channel->outputs[oo].type = O_MIXER;
			mixer_data *mdata = (mixer_data *)(channel->outputs[oo].data);
			const char *name = (const char *)outs[o]["name"];
			if((mdata->mixer = getmixerbyname(name)) == NULL) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"] outputs["<<o<<"]: unknown mixer \""<<name<<"\"\n";
				error();
			}
			float ampfactor = outs[o].exists("ampfactor") ?
				(float)outs[o]["ampfactor"] : 1.0f;
			float balance = outs[o].exists("balance") ?
				(float)outs[o]["balance"] : 0.0f;
			if(balance < -1.0f || balance > 1.0f) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"] outputs["<<o<<"]: balance out of allowed range <-1.0;1.0>\n";
				error();
			}
			if((mdata->input = mixer_connect_input(mdata->mixer, ampfactor, balance)) < 0) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"] outputs["<<o<<"]: "\
					"could not connect to mixer "<<name<<": "<<mixer_get_error()<<"\n";
					error();
			}
			debug_print("dev[%d].chan[%d].out[%d] connected to mixer %s as input %d (ampfactor=%.1f balance=%.1f)\n",
				i, j, o, name, mdata->input, ampfactor, balance);
#ifdef PULSE
		} else if(!strncmp(outs[o]["type"], "pulse", 5)) {
			channel->outputs[oo].data = XCALLOC(1, sizeof(struct pulse_data));
			channel->outputs[oo].type = O_PULSE;

			pulse_data *pdata = (pulse_data *)(channel->outputs[oo].data);
			pdata->continuous = outs[o].exists("continuous") ?
				(bool)(outs[o]["continuous"]) : false;
			pdata->server = outs[o].exists("server") ? strdup(outs[o]["server"]) : NULL;
			pdata->name = outs[o].exists("name") ? strdup(outs[o]["name"]) : "rtl_airband";
			pdata->sink = outs[o].exists("sink") ? strdup(outs[o]["sink"]) : NULL;

			if (outs[o].exists("stream_name")) {
				pdata->stream_name = strdup(outs[o]["stream_name"]);
			} else {
				if(parsing_mixers) {
					cerr<<"Configuration error: mixers.["<<i<<"] outputs["<<o<<"]: PulseAudio outputs of mixers must have stream_name defined\n";
					error();
				}
				char buf[1024];
				snprintf(buf, sizeof(buf), "%.3f MHz", (float)channel->freqlist[0].frequency  / 1000000.0f);
				pdata->stream_name = strdup(buf);
			}
#endif
		} else {
			cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"] outputs["<<o<<"]: unknown output type\n";
			error();
		}
		channel->outputs[oo].enabled = true;
		channel->outputs[oo].active = false;
		oo++;
	}
	return oo;
}

static struct freq_t *mk_freqlist( int n )
{
	if(n < 1) {
		cerr<<"mk_freqlist: invalid list length " << n << "\n";
		error();
	}
	struct freq_t *fl = (struct freq_t *)XCALLOC(n, sizeof(struct freq_t));
	for(int i = 0; i < n; i++) {
		fl[i].frequency = 0;
		fl[i].label = NULL;
		fl[i].agcavgfast = 0.5f;
		fl[i].agcavgslow = 0.5f;
		fl[i].agcmin = 100.0f;
		fl[i].agclow = 0;
		fl[i].sqlevel = -1;
	}
	return fl;
}

static void warn_if_freq_not_in_range(int devidx, int chanidx, int freq, int centerfreq, int sample_rate) {
	static const float soft_bw_threshold = 0.9f;
	float bw_limit = (float)sample_rate / 2.f * soft_bw_threshold;
	if((float)abs(freq - centerfreq) >= bw_limit) {
		log(LOG_WARNING,
			"Warning: dev[%d].channel[%d]: frequency %.3f MHz is outside of SDR operating bandwidth (%.3f-%.3f MHz)\n",
			devidx, chanidx, (double)freq / 1e6,
			(double)(centerfreq - bw_limit) / 1e6,
			(double)(centerfreq + bw_limit) / 1e6);
	}
}

static int parse_anynum2int(libconfig::Setting& f) {
	int ret = 0;
	if(f.getType() == libconfig::Setting::TypeInt) {
		ret = (int)f;
	} else if(f.getType() == libconfig::Setting::TypeFloat) {
		ret = (int)((double)f * 1e6);
	} else if(f.getType() == libconfig::Setting::TypeString) {
		char *s = strdup((char const *)f);
		ret = (int)atofs(s);
		free(s);
	}
	return ret;
}

static int parse_channels(libconfig::Setting &chans, device_t *dev, int i) {
	int jj = 0;
	for (int j = 0; j < chans.getLength(); j++) {
		if(chans[j].exists("disable") && (bool)chans[j]["disable"] == true) {
			continue;
		}
		channel_t* channel = dev->channels + jj;
		for (int k = 0; k < AGC_EXTRA; k++) {
			channel->wavein[k] = 20;
			channel->waveout[k] = 0.5;
		}
		channel->agcsq = 1;
		channel->axcindicate = ' ';
		channel->modulation = MOD_AM;
		channel->mode = MM_MONO;
		channel->need_mp3 = 0;
		channel->freq_count = 1;
		channel->freq_idx = 0;
		channel->highpass = chans[j].exists("highpass") ? (int)chans[j]["highpass"] : 100;
		channel->lowpass = chans[j].exists("lowpass") ? (int)chans[j]["lowpass"] : 2500;
		if(chans[j].exists("modulation")) {
#ifdef NFM
			if(!strncmp(chans[j]["modulation"], "nfm", 3)) {
				channel->modulation = MOD_NFM;
				channel->needs_raw_iq = 1;
			} else
#endif
			if(!strncmp(chans[j]["modulation"], "am", 2)) {
				channel->modulation = MOD_AM;
			} else {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: unknown modulation\n";
				error();
			}
		}
		channel->afc = chans[j].exists("afc") ? (unsigned char) (unsigned int)chans[j]["afc"] : 0;
		if(dev->mode == R_MULTICHANNEL) {
			channel->freqlist = mk_freqlist( 1 );
			channel->freqlist[0].frequency = parse_anynum2int(chans[j]["freq"]);
			warn_if_freq_not_in_range(i, j, channel->freqlist[0].frequency, dev->input->centerfreq, dev->input->sample_rate);
		} else { /* R_SCAN */
			channel->freq_count = chans[j]["freqs"].getLength();
			if(channel->freq_count < 1) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: freqs should be a list with at least one element\n";
				error();
			}
			channel->freqlist = mk_freqlist( channel->freq_count );
			if(chans[j].exists("labels") && chans[j]["labels"].getLength() < channel->freq_count) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: labels should be a list with at least "
					<<channel->freq_count<<" elements\n";
				error();
			}
			if(chans[j].exists("squelch") && libconfig::Setting::TypeList == chans[j]["squelch"].getType() && chans[j]["squelch"].getLength() < channel->freq_count) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: squelch should be an int or a list with at least "
					<<channel->freq_count<<" elements\n";
				error();
			}
			for(int f = 0; f<channel->freq_count; f++) {
				channel->freqlist[f].frequency = parse_anynum2int((chans[j]["freqs"][f]));
				if(chans[j].exists("labels")) {
					channel->freqlist[f].label = strdup(chans[j]["labels"][f]);
				}
			}
// Set initial frequency for scanning
// We tune 20 FFT bins higher to avoid DC spike
			dev->input->centerfreq = channel->freqlist[0].frequency + 20 * (double)(dev->input->sample_rate / fft_size);
		}
		if(chans[j].exists("squelch")) {
			if(libconfig::Setting::TypeList == chans[j]["squelch"].getType()) {
				// New-style array of per-frequency squelch settings
				for(int f = 0; f<channel->freq_count; f++) {
					channel->freqlist[f].sqlevel = (int)chans[j]["squelch"][f];
				}
				// NB: no value check; -1 allows auto-squelch for
				//     some frequencies and not others.
			} else if(libconfig::Setting::TypeInt == chans[j]["squelch"].getType()) {
				// Legacy (single squelch for all frequencies)
				int sqlevel = (int)chans[j]["squelch"];
				if(sqlevel < 0) {
					cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: squelch must be greater than 0\n";
					error();
				}
				for(int f = 0; f<channel->freq_count; f++) {
					channel->freqlist[f].sqlevel = sqlevel;
				}
			} else {
				cerr<<"Invalid value for squelch (should be int or list - use parentheses)\n";
				error();
			}
		}
#ifdef NFM
		if(chans[j].exists("tau")) {
			channel->alpha = ((int)chans[j]["tau"] == 0 ? 0.0f : exp(-1.0f/(WAVE_RATE * 1e-6 * (int)chans[j]["tau"])));
		} else {
			channel->alpha = dev->alpha;
		}
#endif
		libconfig::Setting &outputs = chans[j]["outputs"];
		channel->output_count = outputs.getLength();
		if(channel->output_count < 1) {
			cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: no outputs defined\n";
			error();
		}
		channel->outputs = (output_t *)XCALLOC(channel->output_count, sizeof(struct output_t));
		int outputs_enabled = parse_outputs(outputs, channel, i, j, false);
		if(outputs_enabled < 1) {
			cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: no outputs defined\n";
			error();
		}
		channel->outputs = (output_t *)XREALLOC(channel->outputs, outputs_enabled * sizeof(struct output_t));
		channel->output_count = outputs_enabled;

		dev->base_bins[jj] = dev->bins[jj] = (size_t)ceil(
			   (channel->freqlist[0].frequency + dev->input->sample_rate - dev->input->centerfreq)
			 / (double)(dev->input->sample_rate / fft_size) - 1.0
		) % fft_size;
		debug_print("bins[%d]: %zu\n", jj, dev->bins[jj]);

		if(channel->needs_raw_iq) {
// Downmixing is done only for NFM and raw IQ outputs. It's not critical to have some residual
// freq offset in AM, as it doesn't affect sound quality significantly.
			double dm_dphi = (double)(channel->freqlist[0].frequency - dev->input->centerfreq); // downmix freq in Hz

// In general, sample_rate is not required to be an integer multiple of WAVE_RATE.
// However the FFT window may only slide by an integer number of input samples. A non-zero rounding error
// introduces additional phase rotation which we have to compensate in order to shift the channel of interest
// to the center of the spectrum of the output I/Q stream. This is important for correct NFM demodulation.
// The error value (in Hz):
// - has an absolute value 0..WAVE_RATE/2
// - is linear with the error introduced by rounding the value of sample_rate/WAVE_RATE to the nearest integer
//   (range of -0.5..0.5)
// - is linear with the distance between center frequency and the channel frequency, normalized to 0..1
			double decimation_factor = ((double)dev->input->sample_rate / (double)WAVE_RATE);
			double dm_dphi_correction = (double)WAVE_RATE / 2.0;
			dm_dphi_correction *= (decimation_factor - round(decimation_factor));
			dm_dphi_correction *= (double)(channel->freqlist[0].frequency - dev->input->centerfreq) /
				((double)dev->input->sample_rate/2.0);

			debug_print("dev[%d].chan[%d]: dm_dphi: %f Hz dm_dphi_correction: %f Hz\n",
				i, jj, dm_dphi, dm_dphi_correction);
			dm_dphi -= dm_dphi_correction;
			debug_print("dev[%d].chan[%d]: dm_dphi_corrected: %f Hz\n", i, jj, dm_dphi);
// Normalize
			dm_dphi /= (double)WAVE_RATE;
// Unalias it, to prevent overflow of int during cast
			dm_dphi -= trunc(dm_dphi);
			debug_print("dev[%d].chan[%d]: dm_dphi_normalized=%f\n", i, jj, dm_dphi);
// Translate this to uint32_t range 0x00000000-0x00ffffff
			dm_dphi *= 256.0 * 65536.0;
// Cast it to signed int first, because casting negative float to uint is not portable
			channel->dm_dphi = (uint32_t)((int)dm_dphi);
			debug_print("dev[%d].chan[%d]: dm_dphi_scaled=%f cast=0x%x\n", i, jj, dm_dphi, channel->dm_dphi);
			channel->dm_phi = 0.f;
		}
		jj++;
	}
	return jj;
}

int parse_devices(libconfig::Setting &devs) {
	int devcnt = 0;
	for (int i = 0; i < devs.getLength(); i++) {
		if(devs[i].exists("disable") && (bool)devs[i]["disable"] == true) continue;
		device_t* dev = devices + devcnt;
		if(devs[i].exists("type")) {
			dev->input = input_new(devs[i]["type"]);
			if(dev->input == NULL) {
				cerr<<"Configuration error: devices.["<<i<<"]: unsupported device type\n";
				error();
			}
		} else {
#ifdef WITH_RTLSDR
			cerr<<"Warning: devices.["<<i<<"]: assuming device type \"rtlsdr\", please set \"type\" in the device section.\n";
			dev->input = input_new("rtlsdr");
#else
			cerr<<"Configuration error: devices.["<<i<<"]: mandatory parameter missing: type\n";
			error();
#endif
		}
		assert(dev->input != NULL);
		if(devs[i].exists("sample_rate")) {
			int sample_rate = parse_anynum2int(devs[i]["sample_rate"]);
			if(sample_rate < WAVE_RATE) {
				cerr<<"Configuration error: devices.["<<i<<"]: sample_rate must be greater than "<<WAVE_RATE<<"\n";
				error();
			}
			dev->input->sample_rate = sample_rate;
		}
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
		if(dev->mode == R_MULTICHANNEL) {
			dev->input->centerfreq = parse_anynum2int(devs[i]["centerfreq"]);
		}	// centerfreq for R_SCAN will be set by parse_channels() after frequency list has been read
#ifdef NFM
		if(devs[i].exists("tau")) {
			dev->alpha = ((int)devs[i]["tau"] == 0 ? 0.0f : exp(-1.0f/(WAVE_RATE * 1e-6 * (int)devs[i]["tau"])));
		} else {
			dev->alpha = alpha;
		}
#endif

// Parse hardware-dependent configuration parameters
		if(input_parse_config(dev->input, devs[i]) < 0) {
			// FIXME: get and display error string from input_parse_config
			// Right now it exits the program on failure.
		}
// Some basic sanity checks for crucial parameters which have to be set
// (or can be modified) by the input driver
		assert(dev->input->sfmt != SFMT_UNDEF);
		assert(dev->input->fullscale > 0);
		assert(dev->input->bytes_per_sample > 0);
		assert(dev->input->sample_rate > WAVE_RATE);

// For the input buffer size use a base value and round it up to the nearest multiple
// of FFT_BATCH blocks of input samples.
// ceil is required here because sample rate is not guaranteed to be an integer multiple of WAVE_RATE.
		size_t fft_batch_len = FFT_BATCH * (2 * dev->input->bytes_per_sample *
			(size_t)ceil((double)dev->input->sample_rate / (double)WAVE_RATE));
		dev->input->buf_size = MIN_BUF_SIZE;
		if(dev->input->buf_size % fft_batch_len != 0)
			dev->input->buf_size += fft_batch_len - dev->input->buf_size % fft_batch_len;
		debug_print("dev->input->buf_size: %zu\n", dev->input->buf_size);
		dev->input->buffer = (unsigned char *)XCALLOC(sizeof(unsigned char),
			dev->input->buf_size + 2 * dev->input->bytes_per_sample * fft_size);
		dev->input->bufs = dev->input->bufe = 0;
		dev->waveend = dev->waveavail = dev->row = dev->tq_head = dev->tq_tail = 0;
		dev->last_frequency = -1;

		libconfig::Setting &chans = devs[i]["channels"];
		if(chans.getLength() < 1) {
			cerr<<"Configuration error: devices.["<<i<<"]: no channels configured\n";
			error();
		}
		dev->channels = (channel_t *)XCALLOC(chans.getLength(), sizeof(channel_t));
		dev->bins = (size_t *)XCALLOC(chans.getLength(), sizeof(size_t));
		dev->base_bins = (size_t *)XCALLOC(chans.getLength(), sizeof(size_t));
		dev->channel_count = 0;
		int channel_count = parse_channels(chans, dev, i);
		if(channel_count < 1) {
			cerr<<"Configuration error: devices.["<<i<<"]: no channels enabled\n";
			error();
		}
		if(dev->mode == R_SCAN && channel_count > 1) {
			cerr<<"Configuration error: devices.["<<i<<"]: only one channel is allowed in scan mode\n";
			error();
		}
		dev->channels = (channel_t *)XREALLOC(dev->channels, channel_count * sizeof(channel_t));
		dev->bins = (size_t *)XREALLOC(dev->bins, channel_count * sizeof(size_t));
		dev->base_bins = (size_t *)XREALLOC(dev->base_bins, channel_count * sizeof(size_t));
		dev->channel_count = channel_count;
		devcnt++;
	}
	return devcnt;
}

int parse_mixers(libconfig::Setting &mx) {
	const char *name;
	int mm = 0;
	for(int i = 0; i < mx.getLength(); i++) {
		if(mx[i].exists("disable") && (bool)mx[i]["disable"] == true) continue;
		if((name = mx[i].getName()) == NULL) {
			cerr<<"Configuration error: mixers.["<<i<<"]: undefined mixer name\n";
			error();
		}
		mixer_t *mixer = &mixers[mm];
		debug_print("mm=%d name=%s\n", mm, name);
		mixer->enabled = false;
		mixer->name = strdup(name);
		mixer->interval = MIX_DIVISOR;
		channel_t *channel = &mixer->channel;
		channel->mode = MM_MONO;
		libconfig::Setting &outputs = mx[i]["outputs"];
		channel->output_count = outputs.getLength();
		if(channel->output_count < 1) {
			cerr<<"Configuration error: mixers.["<<i<<"]: no outputs defined\n";
			error();
		}
		channel->outputs = (output_t *)XCALLOC(channel->output_count, sizeof(struct output_t));
		int outputs_enabled = parse_outputs(outputs, channel, i, 0, true);
		if(outputs_enabled < 1) {
			cerr<<"Configuration error: mixers.["<<i<<"]: no outputs defined\n";
			error();
		}
		channel->outputs = (output_t *)XREALLOC(channel->outputs, outputs_enabled * sizeof(struct output_t));
		channel->output_count = outputs_enabled;
		mm++;
	}
	return mm;
}

// vim: ts=4
