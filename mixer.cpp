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
			log(LOG_DEBUG, "getmixerbyname(): %s found at %d\n", name, i);
			return &mixers[i];
		}
	}
	log(LOG_DEBUG, "getmixerbyname(): %s not found\n", name);
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
	return(mixer->input_count++);
}

void mixer_put_samples(mixer_t *mixer, int input, float *samples, unsigned int len) {
	assert(mixer);
	assert(samples);
	assert(input < mixer->input_count);
	memcpy(mixer->inputs[input].wavein, samples, len);
}

// vim: ts=4
