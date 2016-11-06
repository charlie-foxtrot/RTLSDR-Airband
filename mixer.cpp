/*
 * mixer.cpp
 * Mixer related routines
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

#include <cstring>
#include <cstdlib>
#include <cassert>
#include <unistd.h>
#include <sys/time.h>
#include <syslog.h>
#include "rtl_airband.h"

static char *err;

static inline void mixer_set_error(const char *msg) {
	err = strdup(msg);
}

const char *mixer_get_error() {
	return (const char *)err;
}

mixer_t *getmixerbyname(const char *name) {
	for(int i = 0; i < mixer_count; i++) {
		if(!strcmp(mixers[i].name, name)) {
			debug_print("%s found at %d\n", name, i);
			return &mixers[i];
		}
	}
	debug_print("%s not found\n", name);
	return NULL;
}

int mixer_connect_input(mixer_t *mixer) {
	if(!mixer) {
		mixer_set_error("mixer is undefined");
		return(-1);
	}
	int i = mixer->input_count;
	if(i >= MAX_MIXINPUTS) {
		mixer_set_error("too many inputs");
		return(-1);
	}
	if((mixer->inputs[i].wavein = (float *)calloc(WAVE_LEN, sizeof(float))) == NULL) {
		mixer_set_error("failed to allocate sample buffer");
		return(-1);
	}
	if((pthread_mutex_init(&mixer->inputs[i].mutex, NULL)) != 0) {
		mixer_set_error("failed to initialize input mutex");
		return(-1);
	}
	mixer->inputs[i].ready = false;
	return(mixer->input_count++);
}

void mixer_put_samples(mixer_t *mixer, int input_idx, float *samples, unsigned int len) {
	assert(mixer);
	assert(samples);
	assert(input_idx < mixer->input_count);
	mixinput_t *input = &mixer->inputs[input_idx];
	pthread_mutex_lock(&input->mutex);
	memcpy(input->wavein, samples, len * sizeof(float));
	input->ready = true;
	pthread_mutex_unlock(&input->mutex);
}

void *mixer_thread(void *params) {
	struct timeval ts, te;
	int interval_usec = 1e+6 * WAVE_BATCH / WAVE_RATE;
	if(mixer_count <= 0) return 0;
	if(DEBUG) gettimeofday(&ts, NULL);
	while(!do_exit) {
		usleep(interval_usec);
		if(do_exit) return 0;
		for(int i = 0; i < mixer_count; i++) {
			mixer_t *mixer = mixers + i;
			if(mixer->input_count == 0) continue;
			channel_t *channel = &mixer->channel;
			memset(channel->waveout, 0, WAVE_BATCH * sizeof(float));
			for(int j = 0; j < mixer->input_count; j++) {
				mixinput_t *input = mixer->inputs + j;
				pthread_mutex_lock(&input->mutex);
				if(input->ready) {
					for(int s = 0; s < WAVE_BATCH; s++) {
						channel->waveout[s] += input->wavein[s];
					}
					input->ready = false;
				} else {
					debug_print("mixer[%d].input[%d] not ready\n", i, j);
				}
				pthread_mutex_unlock(&input->mutex);
			}
		        if(DEBUG) {
		            gettimeofday(&te, NULL);
		            debug_bulk_print("mixerinput: %lu.%lu %lu\n", te.tv_sec, te.tv_usec, (te.tv_sec - ts.tv_sec) * 1000000UL + te.tv_usec - ts.tv_usec);
		            ts.tv_sec = te.tv_sec;
	        	    ts.tv_usec = te.tv_usec;
			    }
			channel->ready = true;
		}
		// signal output_thread?
	}
	return 0;
}

// vim: ts=4
