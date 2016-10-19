/*
 * util.cpp
 * Miscellaneous routines
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

#include <unistd.h>
#include <syslog.h>
#include <cstdarg>
#include <cstring>
#include <shout/shout.h>
#include <lame/lame.h>
#include "rtl_airband.h"

void error() {
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

void tag_queue_put(device_t *dev, int freq, struct timeval tv) {
	pthread_mutex_lock(&dev->tag_queue_lock);
	dev->tq_head++; dev->tq_head %= TAG_QUEUE_LEN;
	if(dev->tq_head == dev->tq_tail) {
		log(LOG_WARNING, "tag_queue_put: queue overrun");
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
