/*
 *  input-mirisdr.cpp
 *  MiriSDR-specific routines
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h> // FIXME: get rid of this
#include <unistd.h>
#include <mirisdr.h>
#include "input-mirisdr.h"
#include "rtl_airband.h"

/* taken from librtlsdr-keenerd, (c) Kyle Keen */
static int mirisdr_nearest_gain(mirisdr_dev_t *dev, int target_gain) {
	int i, r, err1, err2, count, nearest;
	int *gains;
	r = mirisdr_set_tuner_gain_mode(dev, 1);
	if (r < 0) {
		fprintf(stderr, "WARNING: Failed to enable manual gain.\n");
		return r;
	}
	count = mirisdr_get_tuner_gains(dev, NULL);
	if (count <= 0) {
		return -1;
	}
	gains = (int *)XCALLOC(count, sizeof(int));
	count = mirisdr_get_tuner_gains(dev, gains);
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

void mirisdr_callback(unsigned char *buf, uint32_t len, void *ctx) {
	if(do_exit) return;
	device_t *dev = (device_t*)ctx;
	pthread_mutex_lock(&dev->buffer_lock);
	memcpy(dev->buffer + dev->bufe, buf, len);
	if (dev->bufe == 0) {
		memcpy(dev->buffer + BUF_SIZE, buf, fft_size * 2);
	}
	dev->bufe = dev->bufe + len;
	if (dev->bufe == BUF_SIZE) dev->bufe = 0;
	pthread_mutex_unlock(&dev->buffer_lock);
}

uint32_t mirisdr_find_device_by_serial(const char *s) {
	uint32_t device_count, device;
	char vendor[256] = {0}, product[256] = {0}, serial[256] = {0};
	device_count = mirisdr_get_device_count();
	if(device_count < 1)
		return RTL_DEV_INVALID;	// FIXME: hwtype-independent constant
	for(uint32_t i = 0; i < device_count; i++) {
		mirisdr_get_device_usb_strings(i, vendor, product, serial);
		if (strcmp(s, serial) != 0)
			continue;
		device = i;
		return device;
	}
	return RTL_DEV_INVALID;
}

void *mirisdr_exec(void *params) {
	int r;
	device_t *dev = (device_t *)params;
	mirisdr_dev_t *mirisdr = NULL;

/*	mirisdr_hw_flavour_t hw_flavour;
	switch(flavour) {
	case 0:
		hw_flavour = MIRISDR_HW_DEFAULT;
		break;
	case 1:
		hw_flavour = MIRISDR_HW_SDRPLAY;
		break;
	default:
		fprintf(stderr, "Unknown device variant %u\n", flavour);
		_exit(1);
	} */
/*	int device = mirisdr_verbose_device_search(dev);
	if(device < 0)
		_exit(1); */
	r = mirisdr_open(&mirisdr, MIRISDR_HW_DEFAULT, dev->device);
	if(mirisdr == NULL) {
		fprintf(stderr, "Failed to open mirisdr device #%u: error %d\n", dev->device, r);
		_exit(1);
	}

/*	if(usb_xfer_mode == 0)
		r = mirisdr_set_transfer(mirisdr, "ISOC");
	else if(usb_xfer_mode == 1)
		r = mirisdr_set_transfer(mirisdr, "BULK");
	else {
		fprintf(stderr, "Invalid USB transfer mode\n");
		_exit(1);
	}
	if (r < 0) {
		fprintf(stderr, "Failed to set transfer mode for device #%d: error %d\n", device, r);
		_exit(1);
	}
	fprintf(stderr, "Using USB transfer mode %s\n", mirisdr_get_transfer(mirisdr));
*/
	r = mirisdr_set_sample_rate(mirisdr, dev->sample_rate);
	if (r < 0) {
		log(LOG_ERR, "Failed to set sample rate for device #%d: error %d\n", dev->device, r);
		_exit(1);
	}
	r = mirisdr_set_center_freq(mirisdr, dev->centerfreq - dev->correction);
	if (r < 0) {
		log(LOG_ERR, "Failed to set frequency for device #%d: error %d\n", dev->device, r);
		_exit(1);
	}

	int ngain = mirisdr_nearest_gain(mirisdr, dev->gain);
	if(ngain < 0) {
		log(LOG_ERR, "Failed to read supported gain list for device #%d: error %d\n", dev->device, ngain);
		_exit(1);
	}
	r = mirisdr_set_tuner_gain_mode(mirisdr, 1);
	r |= mirisdr_set_tuner_gain(mirisdr, ngain);
	if (r < 0) {
		log(LOG_ERR, "Failed to set gain to %d for device #%d: error %d\n",
			ngain, dev->device, r);
		_exit(1);
	} else
		log(LOG_ERR, "Device #%d: gain set to %d dB\n", dev->device,
			mirisdr_get_tuner_gain(mirisdr));

	r = mirisdr_set_sample_format(mirisdr, "504_S8");
	if (r < 0) {
		log(LOG_ERR, "Failed to set sample format for device #%d: error %d\n", dev->device, r);
		_exit(1);
	}
	mirisdr_reset_buffer(mirisdr);
	log(LOG_ERR, "MiriSDR device %d started\n", dev->device);
	dev->mirisdr = mirisdr;
	atomic_inc(&device_opened);	// FIXME: avoid global
	dev->failed = 0;
//	dev->sbuf = XCALLOC(MIRISDR_BUFSIZE / sizeof(int16_t), sizeof(float));
// FIXME: configurable bufcnt
	if(mirisdr_read_async(mirisdr, mirisdr_callback, params, MIRISDR_BUFCNT, MIRISDR_BUFSIZE) < 0) {
		log(LOG_WARNING, "MiriSDR device #%d: async read failed, disabling\n", dev->device);
		dev->failed = 1;
		disable_device_outputs(dev);
		atomic_dec(&device_opened);
	}
	return 0;
}
/*
void mirisdr_cancel() {
	if(mirisdr != NULL)
		mirisdr_cancel_async(mirisdr);
} */
