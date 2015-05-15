/*
 * A small utility to convert RTLSDR-Airband configuration file
 * from v1.0 to v2.0 format
 *
 * Copyright (c) 2015 Tomasz Lemiech <szpajder@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <libconfig.h++>

using namespace std;
using namespace libconfig;

int main(int argc, char **argv) {
	if(argc<3) {
		cerr<<"Usage: convert_cfg <old_config_file_name> <new_config_file_name>\n";
		return(1);
	}
	FILE *f;
	f = fopen(argv[1], "r");
	if (f == NULL) {
		cerr<<"Could not read configuration file\n";
		return(1);
	}
	int device_count;
	int cnt = 1;
	int n = fscanf(f, "%d\n", &device_count);
	if (n < 1 || device_count < 1) {
		cerr<<"Configuration error: device count is less than 1?\n";
		return(1);
	}
	Config cfg;
	try {
		Setting& r = cfg.getRoot();
		r.add("devices", Setting::TypeList);
		for (int i = 0; i < device_count; i++) {
			int index, channel_count, gain, centerfreq, correction;
			cnt++;
			n = fscanf(f, "%d %d %d %d %d\n", &index, &channel_count, &gain, &centerfreq, &correction);
			if(n < 5) {
				cerr<<"Error at line "<<cnt<<": need 5 fields, read "<<n<<"\n";
				return(1);
			}
			r["devices"].add(Setting::TypeGroup);
			r["devices"][i].add("index", Setting::TypeInt);
			r["devices"][i].add("gain", Setting::TypeInt);
			r["devices"][i].add("centerfreq", Setting::TypeInt);
			r["devices"][i].add("correction", Setting::TypeInt);
			r["devices"][i]["index"] = index;
			r["devices"][i]["gain"] = gain;
			r["devices"][i]["centerfreq"] = centerfreq;
			r["devices"][i]["correction"] = correction;
			r["devices"][i].add("channels", Setting::TypeList);
			for (int j = 0; j < channel_count; j++)  {
				int port, frequency;
				char hostname[256], mountpoint[256], username[256], password[256];
				cnt++;
				n = fscanf(f, "%120s %d %120s %d %120s %120s\n", hostname, &port, mountpoint, &frequency, username, password);
				if(n < 6) {
					cerr<<"Error at line "<<cnt<<": need 6 fields, read "<<n<<"\n";
					return(1);
				}
				r["devices"][i]["channels"].add(Setting::TypeGroup);
				r["devices"][i]["channels"][j].add("freq", Setting::TypeInt);
				r["devices"][i]["channels"][j].add("server", Setting::TypeString);
				r["devices"][i]["channels"][j].add("port", Setting::TypeInt);
				r["devices"][i]["channels"][j].add("mountpoint", Setting::TypeString);
				r["devices"][i]["channels"][j].add("username", Setting::TypeString);
				r["devices"][i]["channels"][j].add("password", Setting::TypeString);
				r["devices"][i]["channels"][j]["freq"] = frequency;
				r["devices"][i]["channels"][j]["server"] = hostname;
				r["devices"][i]["channels"][j]["port"] = port;
				r["devices"][i]["channels"][j]["mountpoint"] = mountpoint;
				r["devices"][i]["channels"][j]["username"] = username;
				r["devices"][i]["channels"][j]["password"] = password;
			}
		}
		cfg.writeFile(argv[2]);
	} catch(FileIOException e) {
		cerr<<"Cannot write configuration file<<\n";
		return(1);
	} catch(ParseException e) {
		cerr<<"Error while parsing configuration file line "<<e.getLine()<<": "<<e.getError()<<"\n";
		return(1);
	} catch(SettingNotFoundException e) {
		cerr<<"Configuration error: parameter missing: "<<e.getPath()<<"\n";
		return(1);
	} catch(SettingTypeException e) {
		cerr<<"Configuration error: invalid parameter type: "<<e.getPath()<<"\n";
		return(1);
	} catch(ConfigException e) {
		cerr<<"Unhandled config exception\n";
		return(1);
	}
}

