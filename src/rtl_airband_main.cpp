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

#include <fcntl.h>    // O_RDWR
#include <cstring>    // srtcat, strdup, strerror
#include <syslog.h>   // LOG_*
#include <unistd.h>   // getopt, optarg, fork, dup2, close, getpid, usleep
#include <iostream>   // cout, cerr
#include <signal.h>   // sigaction
#include <sys/wait.h> // waitpid

#include <libconfig.h++>

#include "logging.h"
#include "rtl_airband.h"

#ifdef DEBUG
char *debug_path;
#endif

// rtl_airband.cpp
extern int devices_running;
extern int tui;
extern bool log_scan_activity;
extern fm_demod_algo fm_demod;
void* controller_thread(void* params);
void init_demod(demod_params_t *params, Signal *signal, int device_start, int device_end);
void init_output(output_params_t *params, int device_start, int device_end, int mixer_start, int mixer_end);
void *demodulate(void *params);

using namespace std;
using namespace libconfig;

#ifndef __MINGW32__
void sighandler(int sig) {
	log(LOG_NOTICE, "Got signal %d, exiting\n", sig);
	do_exit = 1;
}
#else
BOOL WINAPI sighandler(int signum) {
	if (CTRL_C_EVENT == signum) {
		fprintf(stderr, "Signal caught, exiting!\n");
		do_exit = 1;
		return TRUE;
	}
	return FALSE;
}
#endif

static int count_devices_running() {
	int ret = 0;
	for(int i = 0; i < device_count; i++) {
		if(devices[i].input->state == INPUT_RUNNING) {
			ret++;
		}
	}
	return ret;
}


void usage() {
	cout<<"Usage: rtl_airband [options] [-c <config_file_path>]\n\
\t-h\t\t\tDisplay this help text\n\
\t-f\t\t\tRun in foreground, display textual waterfalls\n\
\t-F\t\t\tRun in foreground, do not display waterfalls (for running as a systemd service)\n";
#ifdef NFM
	cout<<"\t-Q\t\t\tUse quadri correlator for FM demodulation (default is atan2)\n";
#endif
#ifdef DEBUG
	cout<<"\t-d <file>\t\tLog debugging information to <file> (default is "<<DEBUG_PATH<<")\n";
#endif
	cout<<"\t-e\t\t\tPrint messages to standard error (disables syslog logging)\n";
	cout<<"\t-c <config_file_path>\tUse non-default configuration file\n\t\t\t\t(default: "<<CFGFILE<<")\n\
\t-v\t\t\tDisplay version and exit\n";
	exit(EXIT_SUCCESS);
}

int main(int argc, char* argv[]) {
#ifdef WITH_PROFILING
	ProfilerStart("rtl_airband.prof");
#endif
#pragma GCC diagnostic ignored "-Wwrite-strings"
	char *cfgfile = CFGFILE;
	char *pidfile = PIDFILE;
#pragma GCC diagnostic warning "-Wwrite-strings"
	int opt;
	char optstring[16] = "efFhvc:";

#ifdef NFM
	strcat(optstring, "Q");
#endif
#ifdef DEBUG
	strcat(optstring, "d:");
#endif

	while((opt = getopt(argc, argv, optstring)) != -1) {
		switch(opt) {
#ifdef NFM
			case 'Q':
				fm_demod = FM_QUADRI_DEMOD;
				break;
#endif
#ifdef DEBUG
			case 'd':
				debug_path = strdup(optarg);
				break;
#endif
			case 'e':
				do_syslog = 0;
				break;
			case 'f':
				foreground = 1;
				tui = 1;
				break;
			case 'F':
				foreground = 1;
				tui = 0;
				break;
			case 'c':
				cfgfile = optarg;
				break;
			case 'v':
				cout<<"RTLSDR-Airband version "<<RTL_AIRBAND_VERSION<<"\n";
				exit(EXIT_SUCCESS);
			case 'h':
			default:
				usage();
				break;
		}
	}
#ifdef DEBUG
	if(!debug_path) debug_path = strdup(DEBUG_PATH);
	init_debug(debug_path);
#endif
#if !defined (__arm__) && !defined (__aarch64__)
#define cpuid(func,ax,bx,cx,dx)\
	__asm__ __volatile__ ("cpuid":\
		"=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));
	int a,b,c,d;
	cpuid(1,a,b,c,d);
	if((int)((d >> 25) & 0x1)) {
		/* NOOP */
	} else {
		printf("Unsupported CPU.\n");
		error();
	}
#endif /* !__arm__ */

	// If executing other than as root, GPU memory gets alloc'd and the
	// 'permission denied' message on /dev/mem kills rtl_airband without
	// releasing GPU memory.
#ifdef WITH_BCM_VC
	// XXX should probably do this check in other circumstances also.
	if(0 != getuid()) {
		cerr<<"FFT library requires that rtl_airband be executed as root\n";
		exit(1);
	}
#endif

	// read config
	try {
		Config config;
		config.readFile(cfgfile);
		Setting &root = config.getRoot();
		if(root.exists("pidfile")) pidfile = strdup(root["pidfile"]);
		if(root.exists("fft_size")) {
			int fsize = (int)(root["fft_size"]);
			fft_size_log = 0;
			for(size_t i = MIN_FFT_SIZE_LOG; i <= MAX_FFT_SIZE_LOG; i++) {
				if(fsize == 1 << i) {
					fft_size = (size_t)fsize;
					fft_size_log = i;
					break;
				}
			}
			if(fft_size_log == 0) {
				cerr<<"Configuration error: invalid fft_size value (must be a power of two in range "<<
					(1<<MIN_FFT_SIZE_LOG)<<"-"<<(1<<MAX_FFT_SIZE_LOG)<<")\n";
				error();
			}
		}
		if(root.exists("shout_metadata_delay")) shout_metadata_delay = (int)(root["shout_metadata_delay"]);
		if(shout_metadata_delay < 0 || shout_metadata_delay > 2*TAG_QUEUE_LEN) {
			cerr<<"Configuration error: shout_metadata_delay is out of allowed range (0-"<<2 * TAG_QUEUE_LEN<<")\n";
			error();
		}
		if(root.exists("localtime") && (bool)root["localtime"] == true)
			use_localtime = true;
		if(root.exists("multiple_demod_threads") && (bool)root["multiple_demod_threads"] == true) {
#ifdef WITH_BCM_VC
			cerr<<"Using multiple_demod_threads not supported with BCM VideoCore for FFT\n";
			exit(1);
#endif
			multiple_demod_threads = true;
		}
		if(root.exists("multiple_output_threads") && (bool)root["multiple_output_threads"] == true) {
			multiple_output_threads = true;
		}
		if(root.exists("log_scan_activity") && (bool)root["log_scan_activity"] == true)
			log_scan_activity = true;
		if(root.exists("stats_filepath"))
			stats_filepath = strdup(root["stats_filepath"]);
#ifdef NFM
		if(root.exists("tau"))
			alpha = ((int)root["tau"] == 0 ? 0.0f : exp(-1.0f/(WAVE_RATE * 1e-6 * (int)root["tau"])));
#endif
		Setting &devs = config.lookup("devices");
		device_count = devs.getLength();
		if (device_count < 1) {
			cerr<<"Configuration error: no devices defined\n";
			error();
		}

#ifndef __MINGW32__
		struct sigaction sigact, pipeact;

		memset(&sigact, 0, sizeof(sigact));
		memset(&pipeact, 0, sizeof(pipeact));
		pipeact.sa_handler = SIG_IGN;
		sigact.sa_handler = &sighandler;
		sigaction(SIGPIPE, &pipeact, NULL);
		sigaction(SIGHUP, &sigact, NULL);
		sigaction(SIGINT, &sigact, NULL);
		sigaction(SIGQUIT, &sigact, NULL);
		sigaction(SIGTERM, &sigact, NULL);
#else
		SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif

		devices = (device_t *)XCALLOC(device_count, sizeof(device_t));
		shout_init();
		if(do_syslog) openlog("rtl_airband", LOG_PID, LOG_DAEMON);

		if(root.exists("mixers")) {
			Setting &mx = config.lookup("mixers");
			mixers = (mixer_t *)XCALLOC(mx.getLength(), sizeof(struct mixer_t));
			if((mixer_count = parse_mixers(mx)) > 0) {
				mixers = (mixer_t *)XREALLOC(mixers, mixer_count * sizeof(struct mixer_t));
			} else {
				free(mixers);
			}
		} else {
			mixer_count = 0;
		}

		uint32_t devs_enabled = parse_devices(devs);
		if (devs_enabled < 1) {
			cerr<<"Configuration error: no devices defined\n";
			error();
		}
		device_count = devs_enabled;
		debug_print("mixer_count=%d\n", mixer_count);
#ifdef DEBUG
		for(int z = 0; z < mixer_count; z++) {
			mixer_t *m = &mixers[z];
			debug_print("mixer[%d]: name=%s, input_count=%d, output_count=%d\n", z, m->name, m->input_count, m->channel.output_count);
		}
#endif
	} catch(const FileIOException &e) {
			cerr<<"Cannot read configuration file "<<cfgfile<<"\n";
			error();
	} catch(const ParseException &e) {
			cerr<<"Error while parsing configuration file "<<cfgfile<<" line "<<e.getLine()<<": "<<e.getError()<<"\n";
			error();
	} catch(const SettingNotFoundException &e) {
			cerr<<"Configuration error: mandatory parameter missing: "<<e.getPath()<<"\n";
			error();
	} catch(const SettingTypeException &e) {
			cerr<<"Configuration error: invalid parameter type: "<<e.getPath()<<"\n";
			error();
	} catch(const ConfigException &e) {
			cerr<<"Unhandled config exception\n";
			error();
	}

	log(LOG_INFO, "RTLSDR-Airband version %s starting\n", RTL_AIRBAND_VERSION);

#ifndef __MINGW32__ // Fork Were Nowhere Near the Windows
	if(!foreground) {
		int pid1, pid2;
		if((pid1 = fork()) == -1) {
			cerr<<"Cannot fork child process: "<<strerror(errno)<<"\n";
			error();
		}
		if(pid1) {
			waitpid(-1, NULL, 0);
			return(0);
		} else {
			if((pid2 = fork()) == -1) {
				cerr<<"Cannot fork child process: "<<strerror(errno)<<"\n";
				error();
			}
			if(pid2) {
				return(0);
			} else {
				int nullfd, dupfd;
				if((nullfd = open("/dev/null", O_RDWR)) == -1) {
					log(LOG_CRIT, "Cannot open /dev/null: %s\n", strerror(errno));
					error();
				}
				for(dupfd = 0; dupfd <= 2; dupfd++) {
					if(dup2(nullfd, dupfd) == -1) {
						log(LOG_CRIT, "dup2(): %s\n", strerror(errno));
						error();
					}
				}
				if(nullfd > 2) close(nullfd);
				FILE *f = fopen(pidfile, "w");
				if(f == NULL) {
					log(LOG_WARNING, "Cannot write pidfile: %s\n", strerror(errno));
				} else {
					fprintf(f, "%ld\n", (long)getpid());
					fclose(f);
				}
			}
		}
	}
#endif

	for (int i = 0; i < mixer_count; i++) {
		if(mixers[i].enabled == false) {
			continue;		// no inputs connected = no need to initialize output
		}
		channel_t *channel = &mixers[i].channel;
		if(channel->need_mp3) {
			channel->lame = airlame_init(mixers[i].channel.mode, mixers[i].channel.highpass, mixers[i].channel.lowpass);
			channel->lamebuf = (unsigned char *) malloc(sizeof(unsigned char) * LAMEBUF_SIZE);
		}
		for (int k = 0; k < channel->output_count; k++) {
			output_t *output = channel->outputs + k;
			if(output->type == O_ICECAST) {
				shout_setup((icecast_data *)(output->data), channel->mode);
			} else if(output->type == O_UDP_STREAM) {
				udp_stream_data *sdata = (udp_stream_data *)(output->data);
				if (!udp_stream_init(sdata, channel->mode, (size_t)WAVE_BATCH * sizeof(float))) {
					cerr << "Failed to initialize mixer " << i << " output " << k << " - aborting\n";
					error();
				}
#ifdef WITH_PULSEAUDIO
			} else if(output->type == O_PULSE) {
				pulse_init();
				pulse_setup((pulse_data *)(output->data), channel->mode);
#endif
			}
		}
	}
	for (int i = 0; i < device_count; i++) {
		device_t* dev = devices + i;
		for (int j = 0; j < dev->channel_count; j++) {
			channel_t* channel = dev->channels + j;

			// If the channel has icecast or MP3 file output, we will attempt to
			// initialize a separate LAME context for MP3 encoding.
			if(channel->need_mp3) {
				channel->lame = airlame_init(channel->mode, channel->highpass, channel->lowpass);
				channel->lamebuf = (unsigned char *) malloc(sizeof(unsigned char) * LAMEBUF_SIZE);
			}
			for (int k = 0; k < channel->output_count; k++) {
				output_t *output = channel->outputs + k;
				if(output->type == O_ICECAST) {
					shout_setup((icecast_data *)(output->data), channel->mode);
				} else if(output->type == O_UDP_STREAM) {
					udp_stream_data *sdata = (udp_stream_data *)(output->data);
					if (!udp_stream_init(sdata, channel->mode, (size_t)WAVE_BATCH * sizeof(float))) {
						cerr << "Failed to initialize device " << i << " channel " << j << " output " << k << " - aborting\n";
						error();
					}
#ifdef WITH_PULSEAUDIO
				} else if(output->type == O_PULSE) {
					pulse_init();
					pulse_setup((pulse_data *)(output->data), channel->mode);
#endif
				}
			}
		}
		if(input_init(dev->input) != 0 || dev->input->state != INPUT_INITIALIZED) {
			if(errno != 0) {
				cerr<<"Failed to initialize input device "<<i<<": "<<strerror(errno)<<" - aborting\n";
			} else {
				cerr<<"Failed to initialize input device "<<i<<" - aborting\n";
			}
			error();
		}
		if(input_start(dev->input) != 0) {
			cerr<<"Failed to start input on device "<<i<<": "<<strerror(errno)<<" - aborting\n";
			error();
		}
		if(dev->mode == R_SCAN) {
// FIXME: set errno
			if(pthread_mutex_init(&dev->tag_queue_lock, NULL) != 0) {
				cerr<<"Failed to initialize mutex - aborting\n";
				error();
			}
// FIXME: not needed when freq_count == 1?
			pthread_create(&dev->controller_thread, NULL, &controller_thread, dev);
		}
	}

	int timeout = 50;		// 5 seconds
	while ((devices_running = count_devices_running()) != device_count && timeout > 0) {
		SLEEP(100);
		timeout--;
	}
	if((devices_running = count_devices_running()) != device_count) {
		log(LOG_ERR, "%d device(s) failed to initialize - aborting\n", device_count - devices_running);
		error();
	}
	if (tui) {
		printf("\e[1;1H\e[2J");

		GOTOXY(0, 0);
		printf("                                                                               ");
		for (int i = 0; i < device_count; i++) {
			GOTOXY(0, i * 17 + 1);
			for (int j = 0; j < devices[i].channel_count; j++) {
				printf(" %7.3f  ", devices[i].channels[j].freqlist[devices[i].channels[j].freq_idx].frequency / 1000000.0);
			}
			if (i != device_count - 1) {
				GOTOXY(0, i * 17 + 16);
				printf("-------------------------------------------------------------------------------");
			}
		}
	}
	THREAD output_check;
	pthread_create(&output_check, NULL, &output_check_thread, NULL);

	int demod_thread_count = multiple_demod_threads ? device_count : 1;
	demod_params_t *demod_params = (demod_params_t *)XCALLOC(demod_thread_count, sizeof(demod_params_t));
	THREAD *demod_threads = (THREAD *)XCALLOC(demod_thread_count, sizeof(THREAD));

	int output_thread_count = 1;
	if (multiple_output_threads) {
		output_thread_count = demod_thread_count;
		if (mixer_count > 0) {
			output_thread_count++;
		}
	}
	output_params_t *output_params = (output_params_t *)XCALLOC(output_thread_count, sizeof(output_params_t));
	THREAD *output_threads = (THREAD *)XCALLOC(output_thread_count, sizeof(THREAD));

	// Setup the output and demod threads
	if (multiple_output_threads == false) {
		init_output(&output_params[0], 0, device_count, 0, mixer_count);

		if (multiple_demod_threads == false) {
			init_demod(&demod_params[0], output_params[0].mp3_signal, 0, device_count);
		} else {
			for (int i = 0; i < demod_thread_count; i++) {
				init_demod(&demod_params[i], output_params[0].mp3_signal, i, i+1);
			}
		}
	} else {
		if (multiple_demod_threads == false) {
			init_output(&output_params[0], 0, device_count, 0, 0);
			init_demod(&demod_params[0], output_params[0].mp3_signal, 0, device_count);
		} else {
			for (int i = 0; i < device_count; i++)
			{
				init_output(&output_params[i], i, i+1, 0, 0);
				init_demod(&demod_params[i], output_params[i].mp3_signal, i, i+1);
			}
		}
		if (mixer_count > 0) {
			init_output(&output_params[output_thread_count - 1], 0, 0, 0, mixer_count);
		}
	}

	// Startup the output threads
	for (int i = 0; i < output_thread_count; i++)
	{
		pthread_create(&output_threads[i], NULL, &output_thread, &output_params[i]);
	}

	// Startup the mixer thread (if there is one) using the signal for the last output thread
	THREAD mixer;
	if(mixer_count > 0) {
		pthread_create(&mixer, NULL, &mixer_thread, output_params[output_thread_count-1].mp3_signal);
	}

#ifdef WITH_PULSEAUDIO
	pulse_start();
#endif
	sincosf_lut_init();

	// Startup the demod threads
	for (int i = 0; i < demod_thread_count; i++) {
		pthread_create(&demod_threads[i], NULL, &demodulate, &demod_params[i]);
	}

	// Wait for demod threads to exit
	for (int i = 0; i < demod_thread_count; i++) {
		pthread_join(demod_threads[i], NULL);
	}

	log(LOG_INFO, "Cleaning up\n");
	for (int i = 0; i < device_count; i++) {
		if(devices[i].mode == R_SCAN)
			pthread_join(devices[i].controller_thread, NULL);
		if(input_stop(devices[i].input) != 0 || devices[i].input->state != INPUT_STOPPED) {
			if(errno != 0) {
				log(LOG_ERR, "Failed do stop device #%d: %s\n", i, strerror(errno));
			} else {
				log(LOG_ERR, "Failed do stop device #%d\n", i);
			}
		}
	}
	log(LOG_INFO, "Input threads closed\n");

	for (int i = 0; i < device_count; i++) {
		device_t* dev = devices + i;
		disable_device_outputs(dev);
	}

	if(mixer_count > 0) {
		log(LOG_INFO, "Closing mixer thread\n");
		pthread_join(mixer, NULL);
	}

	log(LOG_INFO, "Closing output thread(s)\n");
	for (int i = 0; i < output_thread_count; i++) {
		output_params[i].mp3_signal->send();
		pthread_join(output_threads[i], NULL);
	}

	for (int i = 0; i < device_count; i++) {
		device_t* dev = devices + i;
		for (int j = 0; j < dev->channel_count; j++) {
			channel_t* channel = dev->channels + j;
			if(channel->need_mp3 && channel->lame){
				lame_close(channel->lame);
			}
		}
	}

	close_debug();
#ifdef WITH_PROFILING
	ProfilerStop();
#endif
	return 0;
}
