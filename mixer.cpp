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

#include <stdio.h>

#include <cstring>
#include <cstdlib>
#include <syslog.h>
#include "rtl_airband.h"

static char *err;

static inline void mixer_set_err(const char *msg) {
	err = strdup(msg);
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

void mixer_put(struct mixer_t *mixer, float *samples, size_t len) {
}

// vim: ts=4
