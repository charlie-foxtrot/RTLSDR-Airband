/*
 * input-rtlsdr.cpp
 * RTLSDR-specific routines
 *
 * Copyright (c) 2015-2017 Tomasz Lemiech <szpajder@gmail.com>
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

#include <assert.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h> // FIXME: get rid of this
#include <unistd.h>
#include <rtl-sdr.h>
#include "input-common.h"	// input_t, sample_format_t, input_state_t, MODULE_EXPORT
#include "input-rtlsdr.h"	// rtlsdr_dev_data_t
#include "rtl_airband.h"	// do_exit, fft_size, debug_print, XCALLOC, error()

using namespace std;

static void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
	if(do_exit) return;
	input_t *input = (input_t *)ctx;
	size_t slen = (size_t)len;
/* Write input data into circular buffer dev->buffer.
 * In general, dev->buffer_size is not an exact multiple of len,
 * so we have to take care about proper wrapping.
 * dev->buffer_size is an exact multiple of FFT_BATCH * bps,
 * and dev->buffer's real length is dev->buf_size + 2 * fft_size.
 * On each wrap we copy 2 * fft_size bytes from the start of
 * dev->buffer to its end, so that the signal windowing function
 * could handle the whole FFT batch without wrapping. */
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

/* taken from librtlsdr-keenerd, (c) Kyle Keen */
static int rtlsdr_nearest_gain(rtlsdr_dev_t *dev, int target_gain) {
	int i, r, err1, err2, count, nearest;
	int *gains;
	r = rtlsdr_set_tuner_gain_mode(dev, 1);
	if (r < 0)
		return r;
	count = rtlsdr_get_tuner_gains(dev, NULL);
	if (count <= 0) {
		return -1;
	}
	gains = (int *)XCALLOC(count, sizeof(int));
	count = rtlsdr_get_tuner_gains(dev, gains);
	nearest = gains[0];
	for (i = 0; i < count; i++) {
		err1 = abs(target_gain - nearest);
		err2 = abs(target_gain - gains[i]);
		if (err2 < err1) {
			nearest = gains[i];
		}
	}
	free(gains);
	return nearest;
}

static int rtlsdr_find_device_by_serial(char const * const s) {
	int device_count;
	char vendor[256] = {0}, product[256] = {0}, serial[256] = {0};
	device_count = rtlsdr_get_device_count();
	if(device_count < 1) {
		return -1;
	}
	for(int i = 0; i < device_count; i++) {
		rtlsdr_get_device_usb_strings(i, vendor, product, serial);
		if (strcmp(s, serial) != 0) {
			continue;
		}
		return i;
	}
	return -1;
}

int rtlsdr_init(input_t * const input) {
	rtlsdr_dev_data_t *dev_data = (rtlsdr_dev_data_t *)input->dev_data;
	if(dev_data->serial != NULL) {
		dev_data->index = rtlsdr_find_device_by_serial(dev_data->serial);
		if(dev_data->index < 0) {
			cerr<<"RTLSDR device with serial number "<<dev_data->serial<<" not found\n";
			error();
		}
	}

	dev_data->dev = NULL;
	rtlsdr_open(&dev_data->dev, dev_data->index);
	if(NULL == dev_data->dev) {
		log(LOG_ERR, "Failed to open rtlsdr device #%d.\n", dev_data->index);
		error();
	}

	rtlsdr_dev_t *rtl = dev_data->dev;
	int r = rtlsdr_set_sample_rate(rtl, input->sample_rate);
	if (r < 0) {
		log(LOG_ERR, "Failed to set sample rate for device #%d. Error %d.\n", dev_data->index, r);
	}

	r = rtlsdr_set_center_freq(rtl, input->centerfreq);
	if(r < 0) {
		log(LOG_ERR, "Failed to set center freq for device #%d. Error %d.\n", dev_data->index, r);
	}

	r = rtlsdr_set_freq_correction(rtl, dev_data->correction);
	if(r < 0 && r != -2 ) {
		log(LOG_ERR, "Failed to set freq correction for device #%d. Error %d.\n", dev_data->index, r);
	}

	int ngain = rtlsdr_nearest_gain(rtl, dev_data->gain);
	if(ngain < 0) {
		log(LOG_ERR, "Failed to read supported gain list for device #%d: error %d\n", dev_data->index, ngain);
		error();
	}
	r = rtlsdr_set_tuner_gain_mode(rtl, 1);
	r |= rtlsdr_set_tuner_gain(rtl, ngain);
	if (r < 0) {
		log(LOG_ERR, "Failed to set gain to %0.2f for device #%d: error %d\n",
			(float)ngain / 10.f, dev_data->index, r);
	} else {
		log(LOG_INFO, "Device #%d: gain set to %0.2f dB\n", dev_data->index,
			(float)rtlsdr_get_tuner_gain(rtl) / 10.f);
	}

	r = rtlsdr_set_agc_mode(rtl, 0);
	if (r < 0) {
		log(LOG_ERR, "Failed to disable AGC for device #%d. Error %d.\n", dev_data->index, r);
	}
	rtlsdr_reset_buffer(rtl);
	log(LOG_INFO, "RTLSDR device %d initialized\n", dev_data->index);
	return 0;
}

void *rtlsdr_rx_thread(void *ctx) {
	input_t *input = (input_t *)ctx;
	rtlsdr_dev_data_t *dev_data = (rtlsdr_dev_data_t *)input->dev_data;
	assert(dev_data->dev != NULL);

	input->state = INPUT_RUNNING;
	if(rtlsdr_read_async(dev_data->dev, rtlsdr_callback, ctx, dev_data->bufcnt, RTLSDR_BUFSIZE) < 0) {
		log(LOG_ERR, "RTLSDR device #%d: async read failed, disabling\n", dev_data->index);
		input->state = INPUT_FAILED;
	}
	return 0;
}

int rtlsdr_stop(input_t * const input) {
	rtlsdr_dev_data_t *dev_data = (rtlsdr_dev_data_t *)input->dev_data;
	assert(dev_data->dev != NULL);

	if(rtlsdr_cancel_async(dev_data->dev) < 0) {
		return -1;
	}
	return 0;
}

int rtlsdr_set_centerfreq(input_t * const input, int const centerfreq) {
	rtlsdr_dev_data_t *dev_data = (rtlsdr_dev_data_t *)input->dev_data;
	assert(dev_data->dev != NULL);

	int r = rtlsdr_set_center_freq(dev_data->dev, centerfreq);
	if(r < 0) {
		log(LOG_ERR, "Failed to set centerfreq for RTLSDR device #%d: error %d\n",
			dev_data->index, r);
		return -1;
	}
	return 0;
}

int rtlsdr_parse_config(input_t * const input, libconfig::Setting &cfg) {
	rtlsdr_dev_data_t *dev_data = (rtlsdr_dev_data_t *)input->dev_data;
	if(cfg.exists("serial")) {
		dev_data->serial = strdup(cfg["serial"]);
	} else if(cfg.exists("index")) {
		dev_data->index = (int)cfg["index"];
	} else {
		cerr<<"RTLSDR configuration error: no index and no serial number given\n";
		error();
	}
	if(cfg.exists("gain")) {
		if(cfg["gain"].getType() == libconfig::Setting::TypeInt) {	// backward compatibility
			dev_data->gain = (int)cfg["gain"] * 10;
		} else if(cfg["gain"].getType() == libconfig::Setting::TypeFloat) {
			dev_data->gain = (int)((float)cfg["gain"] * 10.0f);
		}
	} else {
		cerr<<"RTLSDR configuration error: gain is not configured\n";
		error();
	}
	if(dev_data->gain < 0) {
		cerr<<"RTLSDR configuration error: gain value must be positive\n";
		error();
	}
	if(cfg.exists("correction")) {
		dev_data->correction = (int)cfg["correction"];
	}
	if(cfg.exists("num_buffers")) {
		dev_data->bufcnt = (int)(cfg["num_buffers"]);
		if(dev_data->bufcnt < 1) {
			cerr<<"RTLSDR configuration error: num_buffers must be greater than 0\n";
			error();
		}
	}
	return 0;
}

MODULE_EXPORT input_t *rtlsdr_input_new() {
	rtlsdr_dev_data_t *dev_data = (rtlsdr_dev_data_t *)XCALLOC(1, sizeof(rtlsdr_dev_data_t));
	dev_data->index = -1;	// invalid default receiver index
	dev_data->gain = -1;	// invalid default gain value
	dev_data->bufcnt = RTLSDR_DEFAULT_LIBUSB_BUFFER_COUNT;
/*	return &( input_t ){
		.dev_data = dev_data,
		.state = INPUT_UNKNOWN,
		.sfmt = SFMT_U8,
		.sample_rate = RTLSDR_DEFAULT_SAMPLE_RATE,
		.parse_config = &rtlsdr_parse_config,
		.init = &rtlsdr_init,
		.run_rx_thread = &rtlsdr_rx_thread,
		.set_centerfreq = &rtlsdr_set_centerfreq,
		.stop = &rtlsdr_stop
	}; */
	input_t *input = (input_t *)XCALLOC(1, sizeof(input_t));
	input->dev_data = dev_data;
	input->state = INPUT_UNKNOWN;
	input->sfmt = SFMT_U8;
	input->sample_rate = RTLSDR_DEFAULT_SAMPLE_RATE;
	input->parse_config = &rtlsdr_parse_config;
	input->init = &rtlsdr_init;
	input->run_rx_thread = &rtlsdr_rx_thread;
	input->set_centerfreq = &rtlsdr_set_centerfreq;
	input->stop = &rtlsdr_stop;
	return input;
}
