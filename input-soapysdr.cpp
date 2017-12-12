/*
 *  input-soapysdr.cpp
 *  SoapySDR-specific routines
 *
 *  Copyright (c) 2015-2017 Tomasz Lemiech <szpajder@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <assert.h>
#include <stdlib.h>		// calloc
#include <string.h>		// memcpy
#include <syslog.h>		// LOG_* macros
#include <libconfig.h++>	// Setting
#include <SoapySDR/Device.h>	// SoapySDRDevice, SoapySDRDevice_makeStrArgs
#include <SoapySDR/Formats.h>	// SOAPY_SDR_CS constants
#include "input-common.h"	// input_t, sample_format_t, input_state_t, MODULE_EXPORT
#include "input-helpers.h"	// circbuffer_append
#include "input-soapysdr.h"	// soapysdr_dev_data_t
#include "rtl_airband.h"	// do_exit, fft_size, debug_print, XCALLOC, error()

using namespace std;

int soapysdr_parse_config(input_t * const input, libconfig::Setting &cfg) {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)input->dev_data;

	if(cfg.exists("device_string")) {
		dev_data->device_string = strdup(cfg["device_string"]);
	} else {
		cerr<<"SoapySDR configuration error: mandatory parameter missing: device_string\n";
		error();
	}
	if(cfg.exists("gain")) {
		if(cfg["gain"].getType() == libconfig::Setting::TypeInt) {
			dev_data->gain = (double)((int)cfg["gain"]);
		} else if(cfg["gain"].getType() == libconfig::Setting::TypeFloat) {
			dev_data->gain = (double)cfg["gain"];
		}
	} else {
		cerr<<"SoapySDR configuration error: device '"<<dev_data->device_string<<
			"': gain is not configured\n";
		error();
	}
	if(dev_data->gain < 0) {
		cerr<<"SoapySDR configuration error: device '"<<dev_data->device_string<<
			"': gain value must be positive\n";
		error();
	}
	if(cfg.exists("correction")) {
		if(cfg["correction"].getType() == libconfig::Setting::TypeInt) {
			dev_data->correction = (double)((int)cfg["correction"]);
		} else if(cfg["correction"].getType() == libconfig::Setting::TypeFloat) {
			dev_data->correction = (float)cfg["correction"];
		} else {
			cerr<<"SoapySDR configuration error: device '"<<dev_data->device_string<<
				"': correction value must be numeric\n";
			error();
		}
	}
	return 0;
}

int soapysdr_init(input_t * const input) {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)input->dev_data;

	dev_data->dev = SoapySDRDevice_makeStrArgs(dev_data->device_string);
	if (dev_data->dev == NULL) {
		log(LOG_ERR, "Failed to open SoapySDR device '%s': %s\n", dev_data->device_string,
			SoapySDRDevice_lastError());
		error();
	}
	SoapySDRDevice *sdr = dev_data->dev;

	if(SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, input->sample_rate) != 0) {
		log(LOG_ERR, "Failed to set sample rate for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, input->centerfreq, NULL) != 0) {
		log(LOG_ERR, "Failed to set frequency for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setFrequencyCorrection(sdr, SOAPY_SDR_RX, 0, dev_data->correction) != 0) {
		log(LOG_ERR, "Failed to set frequency correction for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setGainMode(sdr, SOAPY_SDR_RX, 0, false) != 0) {
		log(LOG_ERR, "Failed to set gain mode to manual for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, dev_data->gain) != 0) {
		log(LOG_ERR, "Failed to set gain for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		error();
	}
	log(LOG_INFO, "SoapySDR: device '%s': gain set to %.1f dB\n",
		dev_data->device_string, SoapySDRDevice_getGain(sdr, SOAPY_SDR_RX, 0));
	log(LOG_INFO, "SoapySDR: device '%s' initialized\n", dev_data->device_string);
	return 0;
}

void *soapysdr_rx_thread(void *ctx) {
	input_t *input = (input_t *)ctx;
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)input->dev_data;
	SoapySDRDevice *sdr = dev_data->dev;
	assert(sdr != NULL);

	SoapySDRStream *rxStream = NULL;
// FIXME: configurable sample type
	if(SoapySDRDevice_setupStream(sdr, &rxStream, SOAPY_SDR_RX, SOAPY_SDR_CS8, NULL, 0, NULL) != 0) {
		log(LOG_ERR, "Failed to set up stream for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		input->state = INPUT_FAILED;
		goto cleanup;
	}
	if(SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0)) { //start streaming
		log(LOG_ERR, "Failed to activate stream for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		input->state = INPUT_FAILED;
		goto cleanup;
	}
	input->state = INPUT_RUNNING;
	log(LOG_NOTICE, "SoapySDR: device '%s' started\n", dev_data->device_string);

	int8_t buf[SOAPYSDR_BUFSIZE];
	while(!do_exit) {
		void *bufs[] = {buf};		// array of buffers
		int flags;			// flags set by receive operation
		long long timeNs;		// timestamp for receive buffer
// FIXME: this assumes 8 bit samples
		int num_samples = SoapySDRDevice_readStream(sdr, rxStream, bufs, SOAPYSDR_BUFSIZE / 2, &flags, &timeNs, 100000);
		if(num_samples < 0) {
			log(LOG_ERR, "SoapySDR device '%s': readStream failed: %s, disabling\n",
				dev_data->device_string, SoapySDR_errToStr(num_samples));
			input->state = INPUT_FAILED;
			goto cleanup;
		}
		circbuffer_append(input, (unsigned char *)buf, (size_t)(num_samples * 2));
	}
cleanup:
	SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
	SoapySDRDevice_closeStream(sdr, rxStream);
	SoapySDRDevice_unmake(sdr);
	return 0;
}

int soapysdr_set_centerfreq(input_t * const input, int const centerfreq) {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)input->dev_data;
	assert(dev_data->dev != NULL);

	if(SoapySDRDevice_setFrequency(dev_data->dev, SOAPY_SDR_RX, 0, input->centerfreq, NULL) != 0) {
		log(LOG_ERR, "Failed to set frequency for SoapySDR device '%s': %s\n",
			dev_data->device_string, SoapySDRDevice_lastError());
		return -1;
	}
	return 0;
}

MODULE_EXPORT input_t *soapysdr_input_new() {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)XCALLOC(1, sizeof(soapysdr_dev_data_t));
	dev_data->gain = -1.0;	// invalid default gain value
/*	return &( input_t ){
		.dev_data = dev_data,
		.state = INPUT_UNKNOWN,
		.sfmt = SFMT_U8,
		.sample_rate = SOAPYSDR_DEFAULT_SAMPLE_RATE,
		.parse_config = &soapysdr_parse_config,
		.init = &soapysdr_init,
		.run_rx_thread = &soapysdr_rx_thread,
		.set_centerfreq = &soapysdr_set_centerfreq,
		.stop = &soapysdr_stop
	}; */
	input_t *input = (input_t *)XCALLOC(1, sizeof(input_t));
	input->dev_data = dev_data;
	input->state = INPUT_UNKNOWN;
	input->sfmt = SFMT_S8;
	input->sample_rate = SOAPYSDR_DEFAULT_SAMPLE_RATE;
	input->parse_config = &soapysdr_parse_config;
	input->init = &soapysdr_init;
	input->run_rx_thread = &soapysdr_rx_thread;
	input->set_centerfreq = &soapysdr_set_centerfreq;
	input->stop = NULL;
	return input;
}
