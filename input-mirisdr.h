/*
 *  input-mirisdr.h
 *  MiriSDR-specific declarations
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
#include <stdint.h>
#include "rtl_airband.h"
#define MIRISDR_BUFSIZE 320000
#define MIRISDR_BUFCNT 32
// input-mirisdr.cpp
uint32_t mirisdr_find_device_by_serial(const char *s);
void *mirisdr_exec(void *params);
void mirisdr_callback(unsigned char *buf, uint32_t len, void *ctx);
