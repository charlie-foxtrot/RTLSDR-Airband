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
#include <SoapySDR/Device.h>
#include <SoapySDR/Formats.h>
#include <stdio.h> //printf
#include <stdlib.h> //free
#include <string.h> //memcpy
#include <unistd.h> //_exit
#include <syslog.h> // LOG_* macros
#include "input-soapysdr.h"
#include "rtl_airband.h"

using namespace std;

void *soapysdr_exec(void *params) {
	device_t *dev = (device_t *)params;

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
//	SoapySDRKwargs_set(&args, "rtl", dev->device);  // FIXME; char *
	SoapySDRKwargs_set(&args, "rtl", "0");
	SoapySDRDevice *sdr = SoapySDRDevice_make(&args);
	SoapySDRKwargs_clear(&args);

	if (sdr == NULL) {
		log(LOG_ERR, "SoapySDRDevice_make fail: %s\n", SoapySDRDevice_lastError());
		_exit(-1);
	}

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

	if(SoapySDRDevice_setSampleRate(sdr, SOAPY_SDR_RX, 0, dev->sample_rate) != 0) {
		log(LOG_ERR, "Failed to set sample rate for SoapySDR device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
	if(SoapySDRDevice_setFrequency(sdr, SOAPY_SDR_RX, 0, dev->centerfreq, NULL) != 0) {
		log(LOG_ERR, "Failed to set frequency for device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
	if(SoapySDRDevice_setFrequencyCorrection(sdr, SOAPY_SDR_RX, 0, dev->correction) != 0) {
		log(LOG_ERR, "Failed to set frequency correction for device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
	if(SoapySDRDevice_setGainMode(sdr, SOAPY_SDR_RX, 0, false) != 0) {
		log(LOG_ERR, "Failed to set gain mode to manual for device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
// FIXME: should get the configured value in dB, not in tenths of dB
	if(SoapySDRDevice_setGain(sdr, SOAPY_SDR_RX, 0, dev->gain / 10.f) != 0) {
		log(LOG_ERR, "Failed to set gain for device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
	log(LOG_INFO, "Device #%d: gain set to %f dB\n", SoapySDRDevice_getGain(sdr, SOAPY_SDR_RX, 0));

	SoapySDRStream *rxStream;
	if(SoapySDRDevice_setupStream(sdr, &rxStream, SOAPY_SDR_RX, SOAPY_SDR_CS8, NULL, 0, NULL) != 0) {
		printf("Failed to set up stream for SoapySDR device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
	if(SoapySDRDevice_activateStream(sdr, rxStream, 0, 0, 0)) { //start streaming
		printf("Failed to activate stream for SoapySDR device #%d: %s\n", dev->device, SoapySDRDevice_lastError());
		_exit(1);
	}
	dev->soapysdr = sdr;
	dev->failed = 0;
	atomic_inc(&device_opened);
	log(LOG_NOTICE, "SoapySDR device %d started\n", dev->device);

	int8_t buf[SOAPYSDR_BUFSIZE];

	while(!do_exit) {
		void *bufs[] = {buf}; //array of buffers
		int flags; //flags set by receive operation
		long long timeNs; //timestamp for receive buffer
		int num_samples = SoapySDRDevice_readStream(sdr, rxStream, bufs, SOAPYSDR_BUFSIZE / 2, &flags, &timeNs, 100000);
		if(num_samples < 0) {
			log(LOG_ERR, "SoapySDR device #%d: readStream failed: %s, disabling\n",
				dev->device, SoapySDR_errToStr(num_samples));
			dev->failed = 1;
			disable_device_outputs(dev);
			atomic_dec(&device_opened);
			break;
		}
		debug_print("num_samples: %d\n", num_samples);
		size_t slen = (size_t)num_samples * 2;
		pthread_mutex_lock(&dev->buffer_lock);
		size_t space_left = dev->buf_size - dev->bufe;
		if(space_left >= slen) {
			memcpy(dev->buffer + dev->bufe, buf, slen);
			if(dev->bufe == 0) {
				memcpy(dev->buffer + dev->buf_size, dev->buffer, min(slen, 2 * fft_size));
				debug_print("tail_len=%zu\n", min(slen, 2 * fft_size));
			}
		} else {
			memcpy(dev->buffer + dev->bufe, buf, space_left);
			memcpy(dev->buffer, buf + space_left, slen - space_left);
			memcpy(dev->buffer + dev->buf_size, dev->buffer, min(slen - space_left, 2 * fft_size));
			debug_print("buf wrap: space_left=%zu len=%zu bufe=%zu wrap_len=%zu tail_len=%zu\n",
				space_left, slen, dev->bufe, slen - space_left, min(slen - space_left, 2 * fft_size));
		}
		dev->bufe = (dev->bufe + slen) % dev->buf_size;
		pthread_mutex_unlock(&dev->buffer_lock);
	}

	SoapySDRDevice_deactivateStream(sdr, rxStream, 0, 0);
	SoapySDRDevice_closeStream(sdr, rxStream);
	SoapySDRDevice_unmake(sdr);
	return 0;
}
