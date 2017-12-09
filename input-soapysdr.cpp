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
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include "input-common.h"	// input_t, sample_format_t, input_state_t, MODULE_EXPORT
#include "input-soapysdr.h"	// soapysdr_dev_data_t
#include "rtl_airband.h"	// do_exit, fft_size, debug_print, XCALLOC, error()

using namespace std;

int soapysdr_parse_config(input_t * const input, libconfig::Setting &cfg) {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)input->dev_data;

	if(cfg.exists("gain")) {
		if(cfg["gain"].getType() == libconfig::Setting::TypeInt) {
			dev_data->gain = (double)((int)cfg["gain"]);
		} else if(cfg["gain"].getType() == libconfig::Setting::TypeFloat) {
			dev_data->gain = (float)cfg["gain"];
		}
	} else {
		cerr<<"SoapySDR configuration error: gain is not configured\n";
		error();
	}
	if(dev_data->gain < 0) {
		cerr<<"SoapySDR configuration error: gain value must be positive\n";
		error();
	}
	if(cfg.exists("correction")) {
		if(cfg["correction"].getType() == libconfig::Setting::TypeInt) {
			dev_data->correction = (double)((int)cfg["correction"]);
		} else if(cfg["correction"].getType() == libconfig::Setting::TypeFloat) {
			dev_data->correction = (float)cfg["correction"];
		} else {
			cerr<<"SoapySDR configuration error: correction value must be numeric\n";
			error();
		}
	}
	return 0;
}

int soapysdr_init(input_t * const input) {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)input->dev_data;

	//enumerate devices
//	SoapySDRKwargs *results = SoapySDRDevice_enumerate(NULL, &length);
//	for (size_t i = 0; i < length; i++)
//	{
//		printf("Found device #%d: ", (int)i);
//		for (size_t j = 0; j < results[i].size; j++)
//		{
//			printf("%s=%s, ", results[i].keys[j], results[i].vals[j]);
//		}
//		printf("\n");
//	}
//	SoapySDRKwargsList_clear(results, length);

	//create device instance
	//args can be user defined or from the enumeration result
	SoapySDRKwargs args = {};
	SoapySDRKwargs_set(&args, "driver", "rtlsdr");	// FIXME: configurable
//	SoapySDRKwargs_set(&args, "rtl", dev_data->index);  // FIXME; char *
	SoapySDRKwargs_set(&args, "rtl", "0");
	dev_data->dev = SoapySDRDevice_make(&args);
	SoapySDRKwargs_clear(&args);

	if (dev_data->dev == NULL) {
		log(LOG_ERR, "SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
		error();
	}
	SoapySDRDevice *sdr = dev_data->dev;

	//query device info
/*	char** names = SoapySDRDevice_listAntennas(sdr, SOAPY_SDR_RX, 0, &length);
	printf("Rx antennas: ");
	for (size_t i = 0; i < length; i++) printf("%s, ", names[i]);
	printf("\n");
	SoapySDRStrings_clear(&names, length);

	names = SoapySDRDevice_listGains(sdr, SOAPY_SDR_RX, 0, &length);
	printf("Rx gains: ");
	for (size_t i = 0; i < length; i++) printf("%s, ", names[i]);
	printf("\n");
	SoapySDRStrings_clear(&names, length);

	SoapySDRRange *ranges = SoapySDRDevice_getFrequencyRange(sdr, SOAPY_SDR_RX, 0, &length);
	printf("Rx freq ranges: ");
	for (size_t i = 0; i < length; i++) printf("[%g Hz -> %g Hz], ", ranges[i].minimum, ranges[i].maximum);
	printf("\n");
	free(ranges);
*/

	if(SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, input->sample_rate) != 0) {
// FIXME: replace dev_data->index with some meaningful device identification string
		log(LOG_ERR, "Failed to set sample rate for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, input->centerfreq, NULL) != 0) {
		log(LOG_ERR, "Failed to set frequency for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setFrequencyCorrection(sdr, SOAPY_SDR_RX, 0, dev_data->correction) != 0) {
		log(LOG_ERR, "Failed to set frequency correction for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setGainMode(sdr, SOAPY_SDR_RX, 0, false) != 0) {
		log(LOG_ERR, "Failed to set gain mode to manual for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		error();
	}
	if(SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, dev_data->gain) != 0) {
		log(LOG_ERR, "Failed to set gain for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		error();
	}
	log(LOG_INFO, "SoapySDR: Device #%d: gain set to %.1f dB\n", SoapySDRDevice_getGain(sdr, SOAPY_SDR_RX, 0));
	log(LOG_INFO, "SoapySDR: Device #%d initialized\n", dev_data->index);
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
		log(LOG_ERR, "Failed to set up stream for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		input->state = INPUT_FAILED;
		goto cleanup;
	}
	if(SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0)) { //start streaming
		log(LOG_ERR, "Failed to activate stream for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		input->state = INPUT_FAILED;
		goto cleanup;
	}
	input->state = INPUT_RUNNING;
	log(LOG_NOTICE, "SoapySDR device #%d started\n", dev_data->index);

	int8_t buf[SOAPYSDR_BUFSIZE];
	while(!do_exit) {
		void *bufs[] = {buf};		// array of buffers
		int flags;			// flags set by receive operation
		long long timeNs;		// timestamp for receive buffer
// FIXME: this assumes 8 bit samples
		int num_samples = SoapySDRDevice_readStream(sdr, rxStream, bufs, SOAPYSDR_BUFSIZE / 2, &flags, &timeNs, 100000);
		if(num_samples < 0) {
			log(LOG_ERR, "SoapySDR device #%d: readStream failed: %s, disabling\n",
				dev_data->index, SoapySDR_errToStr(num_samples));
			input->state = INPUT_FAILED;
			goto cleanup;
		}
//		debug_print("num_samples: %d\n", num_samples);
// FIXME: this assumes 8 bit samples
		size_t slen = (size_t)num_samples * 2;
		pthread_mutex_lock(&input->buffer_lock);
		size_t space_left = input->buf_size - input->bufe;
		if(space_left >= slen) {
			memcpy(input->buffer + input->bufe, buf, slen);
			if(input->bufe == 0) {
				memcpy(input->buffer + input->buf_size, input->buffer, min(slen, 2 * fft_size));
				debug_print("tail_len=%zu\n", min(slen, 2 * fft_size));
			}
		} else {
			memcpy(input->buffer + input->bufe, buf, space_left);
			memcpy(input->buffer, buf + space_left, slen - space_left);
			memcpy(input->buffer + input->buf_size, input->buffer, min(slen - space_left, 2 * fft_size));
			debug_print("buf wrap: space_left=%zu len=%zu bufe=%zu wrap_len=%zu tail_len=%zu\n",
				space_left, slen, input->bufe, slen - space_left, min(slen - space_left, 2 * fft_size));
		}
		input->bufe = (input->bufe + slen) % input->buf_size;
		pthread_mutex_unlock(&input->buffer_lock);
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
		log(LOG_ERR, "Failed to set frequency for SoapySDR device #%d: %s\n", dev_data->index, SoapySDRDevice_lastError());
		return -1;
	}
	return 0;
}

MODULE_EXPORT input_t *soapysdr_input_new() {
	soapysdr_dev_data_t *dev_data = (soapysdr_dev_data_t *)XCALLOC(1, sizeof(soapysdr_dev_data_t));
	dev_data->index = -1;	// invalid default receiver index
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
