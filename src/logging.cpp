/*
 * logging.cpp
 *
 * Copyright (C) 2022-2023 charlie-foxtrot
 * Copyright (c) 2015-2022 Tomasz Lemiech <szpajder@gmail.com>
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

#include <cstdio>   // fopen()
#include <cstring>  // strerror()
#include <iostream> // cerr()

#include "logging.h"

FILE *debugf;

void error() {
	close_debug();
	_Exit(1);
}

void init_debug (char *file) {
#ifdef DEBUG
	if(!file) return;
	if((debugf = fopen(file, "a")) == NULL) {

		std::cerr << "Could not open debug file " << file << ": " << strerror(errno) << "\n";
		error();
	}
#else
	UNUSED(file);
#endif
}

void close_debug() {
#ifdef DEBUG
	if(!debugf) return;
	fclose(debugf);
#endif
}

