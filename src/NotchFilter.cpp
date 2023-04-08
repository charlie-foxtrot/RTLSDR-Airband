/*
 * RTLSDR AM/NFM demodulator, mixer, streamer and recorder
 *
 * Copyright (c) 2014 Wong Man Hang <microtony@gmail.com>
 * Copyright (c) 2015-2021 Tomasz Lemiech <szpajder@gmail.com>
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

#include <math.h>

#include "logging.h" // needed for debug_print()
#include "NotchFilter.h"

// Default constructor is no filter
NotchFilter::NotchFilter(void) : enabled_(false) {
}

// Notch Filter based on https://www.dsprelated.com/showcode/173.php
NotchFilter::NotchFilter(float notch_freq, float sample_freq, float q): enabled_(true), x{0.0}, y{0.0} {
	if (notch_freq <= 0.0) {
		debug_print("Invalid frequency %f Hz, disabling notch filter\n", notch_freq);
		enabled_ = false;
		return;
	}

	debug_print("Adding notch filter for %f Hz with parameters {%f, %f}\n", notch_freq, sample_freq, q);

	float wo = 2*M_PI*(notch_freq/sample_freq);

	e = 1/(1 + tan(wo/(q*2)));
	p = cos(wo);
	d[0] = e;
	d[1] = 2*e*p;
	d[2] = (2*e-1);

	debug_print("wo:%f e:%f p:%f d:{%f,%f,%f}\n", wo, e, p, d[0], d[1], d[2]);
}

void NotchFilter::apply(float &value) {
	if (!enabled_) {
		return;
	}

	x[0] = x[1];
	x[1] = x[2];
	x[2] = value;

	y[0] = y[1];
	y[1] = y[2];
	y[2] = d[0]*x[2] - d[1]*x[1] + d[0]*x[0] + d[1]*y[1] - d[2]*y[0];

	value = y[2];
}
