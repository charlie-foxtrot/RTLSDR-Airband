/*
 * NotchFilter.h
 *
 * Copyright (c) 2022-2023 charlie-foxtrot
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

#ifndef _NOTCH_FILTER_H
#define _NOTCH_FILTER_H 1

class NotchFilter
{
public:
	NotchFilter(void);
	NotchFilter(float notch_freq, float sample_freq, float q);
	void apply(float &value);

private:
	bool enabled_;
	float e;
	float p;
	float d[3];
	float x[3];
	float y[3];
};

#endif /* _NOTCH_FILTER_H */
