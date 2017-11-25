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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h> // FIXME: get rid of this
#include <unistd.h>
#include <rtl-sdr.h>
#include "input-rtlsdr.h"
#include "rtl_airband.h"

using namespace std;

int rtlsdr_buffers = 10;

void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
	if(do_exit) return;
	device_t *dev = (device_t*)ctx;
	size_t slen = (size_t)len;
/* Write input data into circular buffer dev->buffer.
 * In general, dev->buffer_size is not an exact multiple of len,
 * so we have to take care about proper wrapping.
 * dev->buffer_size is an exact multiple of FFT_BATCH * bps,
 * and dev->buffer's real length is dev->buf_size + 2 * fft_size.
 * On each wrap we copy 2 * fft_size bytes from the start of
 * dev->buffer to its end, so that the signal windowing function
 * could handle the whole FFT batch without wrapping. */
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

uint32_t rtlsdr_find_device_by_serial(const char *s) {
	uint32_t device_count, device;
	char vendor[256] = {0}, product[256] = {0}, serial[256] = {0};
	device_count = rtlsdr_get_device_count();
	if(device_count < 1)
		return RTL_DEV_INVALID;
	for(uint32_t i = 0; i < device_count; i++) {
		rtlsdr_get_device_usb_strings(i, vendor, product, serial);
		if (strcmp(s, serial) != 0)
			continue;
		device = i;
		return device;
	}
	return RTL_DEV_INVALID;
}

void* rtlsdr_exec(void* params) {
	int r;
	device_t *dev = (device_t*)params;

	dev->rtlsdr = NULL;
	rtlsdr_open(&dev->rtlsdr, dev->device);

	if (NULL == dev->rtlsdr) {
		log(LOG_ERR, "Failed to open rtlsdr device #%d.\n", dev->device);
		error();
		return NULL;
	}
	r = rtlsdr_set_sample_rate(dev->rtlsdr, dev->sample_rate);
	if (r < 0) log(LOG_ERR, "Failed to set sample rate for device #%d. Error %d.\n", dev->device, r);
	r = rtlsdr_set_center_freq(dev->rtlsdr, dev->centerfreq);
	if (r < 0) log(LOG_ERR, "Failed to set center freq for device #%d. Error %d.\n", dev->device, r);
	r = rtlsdr_set_freq_correction(dev->rtlsdr, dev->correction);
	if (r < 0 && r != -2 ) log(LOG_ERR, "Failed to set freq correction for device #%d. Error %d.\n", dev->device, r);

	int ngain = rtlsdr_nearest_gain(dev->rtlsdr, dev->gain);
	if(ngain < 0) {
		log(LOG_ERR, "Failed to read supported gain list for device #%d: error %d\n", dev->device, ngain);
		_exit(1);
	}
	r = rtlsdr_set_tuner_gain_mode(dev->rtlsdr, 1);
	r |= rtlsdr_set_tuner_gain(dev->rtlsdr, ngain);
	if (r < 0)
		log(LOG_ERR, "Failed to set gain to %0.2f for device #%d: error %d\n",
			(float)ngain / 10.f, dev->device, r);
	else
		log(LOG_INFO, "Device #%d: gain set to %0.2f dB\n", dev->device,
			(float)rtlsdr_get_tuner_gain(dev->rtlsdr) / 10.f);

	r = rtlsdr_set_agc_mode(dev->rtlsdr, 0);
	if (r < 0) log(LOG_ERR, "Failed to disable AGC for device #%d. Error %d.\n", dev->device, r);
	rtlsdr_reset_buffer(dev->rtlsdr);
	log(LOG_INFO, "Device %d started.\n", dev->device);
	atomic_inc(&device_opened);
	dev->failed = 0;
	if(rtlsdr_read_async(dev->rtlsdr, rtlsdr_callback, params, rtlsdr_buffers, 320000) < 0) {
		log(LOG_WARNING, "Device #%d: async read failed, disabling\n", dev->device);
		dev->failed = 1;
		disable_device_outputs(dev);
		atomic_dec(&device_opened);
	}
	return 0;
}

