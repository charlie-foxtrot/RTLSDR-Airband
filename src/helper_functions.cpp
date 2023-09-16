/*
 * helper_functions.cpp
 *
 * Copyright (C) 2023 charlie-foxtrot
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

#include <cstddef> // size_t
#include <cstring> // strerror
#include <sys/stat.h> // struct stat, S_ISDIR

#include "logging.h"
#include "helper_functions.h"

using namespace std;

int rename_file_if_exists(const string &oldpath, const string &newpath) {
	int ret = rename(oldpath.c_str(), newpath.c_str());
	if(ret < 0) {
		if(errno == ENOENT) {
			return 0;
		} else {
			log(LOG_ERR, "Could not rename %s to %s: %s\n", oldpath.c_str(), newpath.c_str(), strerror(errno));
		}
	}
	return ret;
}

bool dir_exists(const string &dir_path) {
	struct stat st;
	return (stat(dir_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

bool file_exists(const string &file_path) {
	struct stat st;
	return (stat(file_path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
}

bool make_subdirs(const string &basedir, const char *subdirs) {

    // if final directory exists then nothing to do
    string final_path = basedir + "/" + string(subdirs);
    if (dir_exists(final_path)) {
        return true;
    }

    // otherwise make a copy of the subdirs and tokenize by slash
    char *subdirs_copy =  strdup(subdirs);
    const char * delimiter = "/";

    // loop through making directories one at a time
    string dir_path = basedir;
    char *dirname = strtok(subdirs_copy, delimiter);
    while (dirname)
    {
        dir_path += "/" + string(dirname);

        if (mkdir(dir_path.c_str(), 0755) < 0 && errno != EEXIST) {
            log(LOG_ERR, "Could not create directory %s: %s\n", dir_path.c_str(), strerror(errno));
            return false;
        }

        dirname = strtok(nullptr, delimiter);
    }

    return dir_exists(final_path);
}

string make_dated_subdirs(const string &basedir, const struct tm *time) {

    // use the time to build the date subdirectories
    char date_path[11];
    strftime(date_path, sizeof(date_path), "%Y/%m/%d", time);

    // make all the subdirectories, and return the full path if successful
    if (make_subdirs(basedir, date_path)) {
        return basedir + "/" + date_path;
    }

    // on any error return empty string
    return "";
}
