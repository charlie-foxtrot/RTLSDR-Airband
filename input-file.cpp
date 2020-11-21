/*
 * input-file.cpp
 * binary file specific routines
 *
 * Copyright (c) 2015-2020 Tomasz Lemiech <szpajder@gmail.com>
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
#include <assert.h>
#include <limits.h>			// SCHAR_MAX
#include <string.h>
#include <syslog.h>			// FIXME: get rid of this
#include <unistd.h>			// usleep
#include <libconfig.h++>	// Setting
#include "input-common.h"	// input_t, sample_format_t, input_state_t, MODULE_EXPORT
#include "input-helpers.h"	// circbuffer_append
#include "input-file.h"		// file_dev_data_t
#include "rtl_airband.h"	// do_exit, fft_size, debug_print, XCALLOC, error()

using namespace std;

int file_parse_config(input_t * const input, libconfig::Setting &cfg) {
	assert(input != NULL);
	file_dev_data_t *dev_data = (file_dev_data_t *)input->dev_data;
	assert(dev_data != NULL);

	if(cfg.exists("filepath")) {
		dev_data->filepath = (const char *)cfg["filepath"];
	} else {
		cerr << "File configuration error: no 'filepath' given\n";
		error();
	}

	return 0;
}

int file_init(input_t * const input) {
	assert(input != NULL);
	file_dev_data_t *dev_data = (file_dev_data_t *)input->dev_data;
	assert(dev_data != NULL);

	dev_data->input_file = fopen(dev_data->filepath.c_str(), "rb");
	if(!dev_data->input_file) {
		cerr << "File input failed to open '" << dev_data->filepath << "' - " << strerror(errno) << endl;
		error();
	}

	log(LOG_INFO, "File input %s initialized\n", dev_data->filepath.c_str());
	return 0;
}

void *file_rx_thread(void *ctx) {
	input_t *input = (input_t *)ctx;
	assert(input != NULL);
	file_dev_data_t *dev_data = (file_dev_data_t *)input->dev_data;
	assert(dev_data != NULL);
	assert(dev_data->input_file != NULL);

	size_t buf_len = (input->buf_size/2) - 1;
	unsigned char *buf = (unsigned char *)XCALLOC(1, buf_len);

	input->state = INPUT_RUNNING;

	while(true) {
		if(do_exit) {
			break;
		}
		if(feof(dev_data->input_file)) {
			log(LOG_INFO, "File '%s': hit end of file at %d, disabling\n", dev_data->filepath.c_str(), ftell(dev_data->input_file));
			input->state = INPUT_FAILED;
			break;
		}
		if(ferror(dev_data->input_file)) {
			log(LOG_ERR, "File '%s': read error (%d), disabling\n", dev_data->filepath.c_str(), ferror(dev_data->input_file));
			input->state = INPUT_FAILED;
			break;
		}

		size_t space_left;
		pthread_mutex_lock(&input->buffer_lock);
		if (input->bufe >= input->bufs) {
			space_left = input->bufs + (input->buf_size - input->bufe);
		} else {
			space_left = input->bufs - input->bufe;
		}
		pthread_mutex_unlock(&input->buffer_lock);

		if (space_left > buf_len) {
			size_t len = fread(buf, sizeof(unsigned char), buf_len, dev_data->input_file);
			circbuffer_append(input, buf, len);
		} else {
			SLEEP(10);
		}
	}

	free(buf);
	return 0;
}

int file_set_centerfreq(input_t * const /*input*/, int const /*centerfreq*/) {
	return 0;
}

int file_stop(input_t * const input) {
	assert(input != NULL);
	file_dev_data_t *dev_data = (file_dev_data_t *)input->dev_data;
	assert(dev_data != NULL);
	fclose(dev_data->input_file);
	dev_data->input_file = NULL;
	return 0;
}

MODULE_EXPORT input_t *file_input_new() {
	file_dev_data_t *dev_data = (file_dev_data_t *)XCALLOC(1, sizeof(file_dev_data_t));
	dev_data->input_file = NULL;

	input_t *input = (input_t *)XCALLOC(1, sizeof(input_t));
	input->dev_data = dev_data;
	input->state = INPUT_UNKNOWN;
	input->sfmt = SFMT_U8;
	input->fullscale = (float)SCHAR_MAX - 0.5f;
	input->bytes_per_sample = sizeof(unsigned char);
	input->sample_rate = 0;
	input->parse_config = &file_parse_config;
	input->init = &file_init;
	input->run_rx_thread = &file_rx_thread;
	input->set_centerfreq = &file_set_centerfreq;
	input->stop = &file_stop;

	return input;
}

// vim: ts=4
