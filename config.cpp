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

#include <libconfig.h++>
#include "rtl_airband.h"

using namespace std;

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
		int jj = 0;
		for (int j = 0; j < devs[i]["channels"].getLength(); j++) {
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
			channel->need_mp3 = 0;
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
					channel->need_mp3 = 1;
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
					channel->need_mp3 = 1;
				} else if(!strncmp(devs[i]["channels"][j]["outputs"][o]["type"], "mixer", 5)) {
					channel->outputs[oo].data = malloc(sizeof(struct mixer_data));
					if(channel->outputs[oo].data == NULL) {
						cerr<<"Cannot allocate memory for outputs\n";
						error();
					}
					memset(channel->outputs[oo].data, 0, sizeof(struct mixer_data));
					channel->outputs[oo].type = O_MIXER;
					mixer_data *mdata = (mixer_data *)(channel->outputs[oo].data);
					if((mdata->mixer = getmixerbyname((const char *)devs[i]["channels"][j]["outputs"][o]["name"])) == NULL) {
						cerr<<"Configuration error: devices.["<<i<<"] channels.["<<j<<"] outputs["<<o<<"]: unknown mixer \""<< \
							(const char *)devs[i]["channels"][j]["outputs"][o]["name"]<<"\"\n";
						error();
					}
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
		devcnt++;
	}
	return devcnt;
}

