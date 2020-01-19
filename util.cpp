/*
 * util.cpp
 * Miscellaneous routines
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

#include <unistd.h>
#include <stdint.h>			// uint32_t
#include <syslog.h>
#include <iostream>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <cerrno>
#include <cmath>
#include <shout/shout.h>
#include <lame/lame.h>
#include "rtl_airband.h"


FILE *debugf;

void error() {
	close_debug();
	_exit(1);
}

int atomic_inc(volatile int *pv)
{
	return __sync_fetch_and_add(pv, 1);
}

int atomic_dec(volatile int *pv)
{
	return __sync_fetch_and_sub(pv, 1);
}

int atomic_get(volatile int *pv)
{
	return __sync_fetch_and_add(pv, 0);
}

void log(int priority, const char *format, ...) {
	va_list args;
	va_start(args, format);
	if(do_syslog)
		vsyslog(priority, format, args);
	else if(foreground)
		vprintf(format, args);
	va_end(args);
}

void init_debug (char *file) {
	if(DEBUG) {
		if(!file) return;
		if((debugf = fopen(file, "a")) == NULL) {
			std::cerr<<"Could not open debug file "<<file<<": "<<strerror(errno)<<"\n";
			error();
		}
	}
}

void close_debug() {
	if(DEBUG) {
		if(!debugf) return;
		fclose(debugf);
	}
}

void tag_queue_put(device_t *dev, int freq, struct timeval tv) {
	pthread_mutex_lock(&dev->tag_queue_lock);
	dev->tq_head++; dev->tq_head %= TAG_QUEUE_LEN;
	if(dev->tq_head == dev->tq_tail) {
		log(LOG_WARNING, "tag_queue_put: queue overrun\n");
		dev->tq_tail++;
	}
	dev->tag_queue[dev->tq_head].freq = freq;
	memcpy(&dev->tag_queue[dev->tq_head].tv, &tv, sizeof(struct timeval));
	pthread_mutex_unlock(&dev->tag_queue_lock);
}

void tag_queue_get(device_t *dev, struct freq_tag *tag) {
	int i;

	if(!tag) return;
	pthread_mutex_lock(&dev->tag_queue_lock);
	if(dev->tq_head == dev->tq_tail) {	/* empty queue */
		tag->freq = -1;
	} else {
// read queue entry at pos tq_tail+1 without dequeueing it
		i = dev->tq_tail+1; i %= TAG_QUEUE_LEN;
		tag->freq = dev->tag_queue[i].freq;
		memcpy(&tag->tv, &dev->tag_queue[i].tv, sizeof(struct timeval));
	}
	pthread_mutex_unlock(&dev->tag_queue_lock);
}

void tag_queue_advance(device_t *dev) {
	pthread_mutex_lock(&dev->tag_queue_lock);
	dev->tq_tail++; dev->tq_tail %= TAG_QUEUE_LEN;
	pthread_mutex_unlock(&dev->tag_queue_lock);
}

void *xcalloc(size_t nmemb, size_t size, const char *file, const int line, const char *func) {
	void *ptr = calloc(nmemb, size);
	if(ptr == NULL) {
		log(LOG_ERR, "%s:%d: %s(): calloc(%zu, %zu) failed: %s\n",
			file, line, func, nmemb, size, strerror(errno));
		error();
	}
	return ptr;
}

void *xrealloc(void *ptr, size_t size, const char *file, const int line, const char *func) {
	ptr = realloc(ptr, size);
	if(ptr == NULL) {
		log(LOG_ERR, "%s:%d: %s(): realloc(%zu) failed: %s\n",
			file, line, func, size, strerror(errno));
		error();
	}
	return ptr;
}

static float sin_lut[257], cos_lut[257];

void sincosf_lut_init() {
	for(uint32_t i = 0; i < 256; i++)
		sincosf(2.0f * M_PI * (float)i / 256.0f, sin_lut + i, cos_lut + i);
	sin_lut[256] = sin_lut[0];
	cos_lut[256] = cos_lut[0];
}

// phi range must be (0..1), rescaled to 0x0-0xFFFFFF
void sincosf_lut(uint32_t phi, float *sine, float *cosine) {
	float v1, v2, fract;
	uint32_t idx;
// get LUT index
	idx = phi >> 16;
// cast fixed point fraction to float
	fract = (float)(phi & 0xffff) / 65536.0f;
// get two adjacent values from LUT and interpolate
	v1 = sin_lut[idx];
	v2 = sin_lut[idx+1];
	*sine = v1 + (v2 - v1) * fract;
	v1 = cos_lut[idx];
	v2 = cos_lut[idx+1];
	*cosine = v1 + (v2 - v1) * fract;
}

/* librtlsdr-keenerd, (c) Kyle Keen */
double atofs(char *s) {
	char last;
	int len;
	double suff = 1.0;
	len = strlen(s);
	last = s[len-1];
	s[len-1] = '\0';
	switch (last) {
		case 'g':
		case 'G':
			suff *= 1e3;
		case 'm':
		case 'M':
			suff *= 1e3;
		case 'k':
		case 'K':
			suff *= 1e3;
			suff *= atof(s);
			s[len-1] = last;
			return suff;
	}
	s[len-1] = last;
	return atof(s);
}

// vim: ts=4
