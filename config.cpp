/*
 * config.cpp
 * Configuration parsing routines
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

#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <syslog.h>
#include <libconfig.h++>
#include "rtl_airband.h"

using namespace std;

static int parse_outputs(libconfig::Setting &outs, channel_t *channel, int i, int j, bool parsing_mixers) {
	int oo = 0;
	for(int o = 0; o < channel->output_count; o++) {
		if(outs[o].exists("disable") && (bool)outs[o]["disable"] == true) {
			continue;
		}
		if(!strncmp(outs[o]["type"], "icecast", 7)) {
			channel->outputs[oo].data = malloc(sizeof(struct icecast_data));
			if(channel->outputs[oo].data == NULL) {
				cerr<<"Cannot allocate memory for outputs\n";
				error();
			}
			memset(channel->outputs[oo].data, 0, sizeof(struct icecast_data));
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
			if(outs[o].exists("send_scan_freq_tags"))
				idata->send_scan_freq_tags = (bool)outs[o]["send_scan_freq_tags"];
			else
				idata->send_scan_freq_tags = 0;
			channel->need_mp3 = 1;
		} else if(!strncmp(outs[o]["type"], "file", 4)) {
			channel->outputs[oo].data = malloc(sizeof(struct file_data));
			if(channel->outputs[oo].data == NULL) {
				cerr<<"Cannot allocate memory for outputs\n";
				error();
			}
			memset(channel->outputs[oo].data, 0, sizeof(struct file_data));
			channel->outputs[oo].type = O_FILE;
			file_data *fdata = (file_data *)(channel->outputs[oo].data);
			fdata->dir = strdup(outs[o]["directory"]);
			fdata->prefix = strdup(outs[o]["filename_template"]);
			fdata->continuous = outs[o].exists("continuous") ?
				(bool)(outs[o]["continuous"]) : false;
			fdata->append = (!outs[o].exists("append")) || (bool)(outs[o]["append"]);
			channel->need_mp3 = 1;
		} else if(!strncmp(outs[o]["type"], "mixer", 5)) {
			if(parsing_mixers) {	// mixer outputs not allowed for mixers
				cerr<<"Configuration error: mixers.["<<i<<"] outputs["<<o<<"]: mixer output is not allowed for mixers\n";
				error();
			}
			channel->outputs[oo].data = malloc(sizeof(struct mixer_data));
			if(channel->outputs[oo].data == NULL) {
				cerr<<"Cannot allocate memory for outputs\n";
				error();
			}
			memset(channel->outputs[oo].data, 0, sizeof(struct mixer_data));
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
		channel->agcavgfast = 0.5f;
		channel->agcavgslow = 0.5f;
		channel->agcmin = 100.0f;
		channel->agclow = 0;
		channel->sqlevel = -1.0f;
		channel->modulation = MOD_AM;
		channel->mode = MM_MONO;
		channel->need_mp3 = 0;
		if(chans[j].exists("modulation")) {
#ifdef NFM
			if(!strncmp(chans[j]["modulation"], "nfm", 3)) {
				channel->modulation = MOD_NFM;
			} else
#endif
			if(!strncmp(chans[j]["modulation"], "am", 2)) {
				channel->modulation = MOD_AM;
			} else {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: unknown modulation\n";
				error();
			}
		}
		if(chans[j].exists("squelch")) {
			channel->sqlevel = (int)chans[j]["squelch"];
			if(channel->sqlevel <= 0) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: squelch must be greater than 0\n";
				error();
			}
		}
		channel->afc = chans[j].exists("afc") ? (unsigned char) (unsigned int)chans[j]["afc"] : 0;
		if(dev->mode == R_MULTICHANNEL) {
			channel->frequency = chans[j]["freq"];
		} else { /* R_SCAN */
			channel->freq_count = chans[j]["freqs"].getLength();
			if(channel->freq_count < 1) {
				cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: freqs should be a list with at least one element\n";
				error();
			}
			if(chans[j].exists("labels") && chans[j]["labels"].getLength() < channel->freq_count) {
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
				channel->freqlist[f] = (int)(chans[j]["freqs"][f]);
				if(chans[j].exists("labels"))
					channel->labels[f] = strdup(chans[j]["labels"][f]);
			}
// Set initial frequency for scanning
// We tune 2 FFT bins higher to avoid DC spike
			channel->frequency = channel->freqlist[0];
			dev->centerfreq = channel->freqlist[0] + 2 * (double)(SOURCE_RATE / FFT_SIZE);
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
		channel->outputs = (output_t *)malloc(channel->output_count * sizeof(struct output_t));
		if(channel->outputs == NULL) {
			cerr<<"Cannot allocate memory for outputs\n";
			error();
		}
		int outputs_enabled = parse_outputs(outputs, channel, i, j, false);
		if(outputs_enabled < 1) {
			cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"]: no outputs defined\n";
			error();
		}
		channel->outputs = (output_t *)realloc(channel->outputs, outputs_enabled * sizeof(struct output_t));
		if(channel->outputs == NULL) {
			cerr<<"Cannot allocate memory for outputs\n";
			error();
		}
		channel->output_count = outputs_enabled;

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
	return jj;
}

int parse_devices(libconfig::Setting &devs) {
	int devcnt = 0;
	for (int i = 0; i < devs.getLength(); i++) {
		if(devs[i].exists("disable") && (bool)devs[i]["disable"] == true) continue;
		device_t* dev = devices + devcnt;
		if(!devs[i].exists("correction")) devs[i].add("correction", libconfig::Setting::TypeInt);
		dev->device = (int)devs[i]["index"];
		dev->channel_count = 0;
		if(devs[i].exists("gain"))
			dev->gain = (int)devs[i]["gain"] * 10;
		else {
			cerr<<"Configuration error: devices.["<<i<<"]: gain is not configured\n";
			error();
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

		libconfig::Setting &chans = devs[i]["channels"];
		int chans_enabled = parse_channels(chans, dev, i);
		if(chans_enabled < 1 || chans_enabled > 8) {
			cerr<<"Configuration error: devices.["<<i<<"]: invalid channel count (min 1, max 8)\n";
			error();
		}
		if(dev->mode == R_SCAN && chans_enabled > 1) {
			cerr<<"Configuration error: devices.["<<i<<"]: only one channel section is allowed in scan mode\n";
			error();
		}
		dev->channel_count = chans_enabled;
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
		channel->outputs = (output_t *)calloc(channel->output_count, sizeof(struct output_t));
		if(channel->outputs == NULL) {
			cerr<<"Cannot allocate memory for outputs\n";
			error();
		}
		int outputs_enabled = parse_outputs(outputs, channel, i, 0, true);
		if(outputs_enabled < 1) {
			cerr<<"Configuration error: mixers.["<<i<<"]: no outputs defined\n";
			error();
		}
		channel->outputs = (output_t *)realloc(channel->outputs, outputs_enabled * sizeof(struct output_t));
		if(channel->outputs == NULL) {
			cerr<<"Cannot allocate memory for outputs\n";
			error();
		}
		channel->output_count = outputs_enabled;
		mm++;
	}
	return mm;
}

// vim: ts=4
