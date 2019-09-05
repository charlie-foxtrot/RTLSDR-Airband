/*
 * output.cpp
 * Output related routines
 *
 * Copyright (c) 2015-2018 Tomasz Lemiech <szpajder@gmail.com>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <ogg/ogg.h>
#include <vorbis/vorbisenc.h>
#include <shout/shout.h>
#include <lame/lame.h>
#ifdef PULSE
#include <pulse/pulseaudio.h>
#endif
#include <syslog.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cerrno>
#include "rtl_airband.h"
#include "input-common.h"

void shout_setup(icecast_data *icecast, mix_modes mixmode) {
	int ret;
	shout_t * shouttemp = shout_new();
	if (shouttemp == NULL) {
		printf("cannot allocate\n");
	}
	if (shout_set_host(shouttemp, icecast->hostname) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if (shout_set_protocol(shouttemp, SHOUT_PROTOCOL_HTTP) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if (shout_set_port(shouttemp, icecast->port) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	char mp[100];
	sprintf(mp, "/%s", icecast->mountpoint);
	if (shout_set_mount(shouttemp, mp) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if (shout_set_user(shouttemp, icecast->username) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if (shout_set_password(shouttemp, icecast->password) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if (shout_set_format(shouttemp, SHOUT_FORMAT_MP3) != SHOUTERR_SUCCESS){
		shout_free(shouttemp); return;
	}
	if(icecast->name && shout_set_name(shouttemp, icecast->name) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if(icecast->genre && shout_set_genre(shouttemp, icecast->genre) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	if(icecast->description && shout_set_description(shouttemp, icecast->description) != SHOUTERR_SUCCESS) {
		shout_free(shouttemp); return;
	}
	char samplerates[20];
	sprintf(samplerates, "%d", MP3_RATE);
	shout_set_audio_info(shouttemp, SHOUT_AI_SAMPLERATE, samplerates);
	shout_set_audio_info(shouttemp, SHOUT_AI_CHANNELS, (mixmode == MM_STEREO ? "2" : "1"));

	if (shout_set_nonblocking(shouttemp, 1) != SHOUTERR_SUCCESS) {
		log(LOG_ERR, "Error setting non-blocking mode: %s\n", shout_get_error(shouttemp));
		return;
	}
	ret = shout_open(shouttemp);
	if (ret == SHOUTERR_SUCCESS)
		ret = SHOUTERR_CONNECTED;

	if (ret == SHOUTERR_BUSY)
		log(LOG_NOTICE, "Connecting to %s:%d/%s...\n",
			icecast->hostname, icecast->port, icecast->mountpoint);

	int shout_timeout = 30 * 5;		// 30 * 5 * 200ms = 30s
	while (ret == SHOUTERR_BUSY && shout_timeout-- > 0) {
		SLEEP(200);
		ret = shout_get_connected(shouttemp);
	}

	if (ret == SHOUTERR_CONNECTED) {
		log(LOG_NOTICE, "Connected to %s:%d/%s\n",
			icecast->hostname, icecast->port, icecast->mountpoint);
		SLEEP(100);
		icecast->shout = shouttemp;
	} else {
		log(LOG_WARNING, "Could not connect to %s:%d/%s\n",
			icecast->hostname, icecast->port, icecast->mountpoint);
		shout_close(shouttemp);
		shout_free(shouttemp);
		return;
	}
}

lame_t airlame_init(mix_modes mixmode, int highpass, int lowpass) {
	lame_t lame = lame_init();
	if (!lame) {
		log(LOG_WARNING, "lame_init failed\n");
		return NULL;
	}

	lame_set_in_samplerate(lame, WAVE_RATE);
	lame_set_VBR(lame, vbr_mtrh);
	lame_set_brate(lame, 16);
	lame_set_quality(lame, 7);
	lame_set_lowpassfreq(lame, lowpass);
	lame_set_highpassfreq(lame, highpass);
	lame_set_out_samplerate(lame, MP3_RATE);
	if(mixmode == MM_STEREO) {
		lame_set_num_channels(lame, 2);
		lame_set_mode(lame, JOINT_STEREO);
	} else {
		lame_set_num_channels(lame, 1);
		lame_set_mode(lame, MONO);
	}
	debug_print("mixmode=%s\n", mixmode == MM_STEREO ? "MM_STEREO" : "MM_MONO");
	lame_init_params(lame);
	return lame;
}

class LameTone
{
	unsigned char* _data;
	int _bytes;

public:
	LameTone(mix_modes mixmode, int msec, unsigned int hz = 0) : _data(NULL), _bytes(0) {
		_data = (unsigned char *)XCALLOC(1, LAMEBUF_SIZE);

		int samples = (msec * WAVE_RATE) / 1000;
		float *buf = (float *)XCALLOC(samples, sizeof(float));

		if (hz > 0) {
			const float period = 1.0 / (float)hz;
			const float sample_time = 1.0 / (float)WAVE_RATE;
			float t = 0;
			for (int i = 0; i < samples; ++i, t+= sample_time) {
				buf[i] = 0.9 * sinf(t * 2.0 * M_PI / period);
			}
		} else
			memset(buf, 0, samples * sizeof(float));
		lame_t lame = airlame_init(mixmode, 0, 0);
		if (lame) {
			_bytes = lame_encode_buffer_ieee_float(lame, buf, (mixmode == MM_STEREO ? buf : NULL), samples, _data, LAMEBUF_SIZE);
			if (_bytes > 0) {
				int flush_ofs = _bytes;
				if (flush_ofs&0x1f)
					flush_ofs+= 0x20 - (flush_ofs&0x1f);
				if (flush_ofs < LAMEBUF_SIZE) {
					int flush_bytes = lame_encode_flush(lame, _data + flush_ofs, LAMEBUF_SIZE - flush_ofs);
					if (flush_bytes > 0) {
						memmove(_data + _bytes, _data + flush_ofs, flush_bytes);
						_bytes+= flush_bytes;
					}
				}
			}
			else
				log(LOG_WARNING, "lame_encode_buffer_ieee_float: %d\n", _bytes);
			lame_close(lame);
		}
		free(buf);
	}

	~LameTone() {
		if (_data)
			free(_data);
	}

	int write(FILE *f) {
		if (!_data || _bytes<=0)
			return 1;

		if(fwrite(_data, 1, _bytes, f) != (unsigned int)_bytes) {
			log(LOG_WARNING, "LameTone: failed to write %d bytes\n", _bytes);
			return -1;
		}

		return 0;
	}
};

static int fdata_open(file_data *fdata, const char *filename, mix_modes mixmode, int is_audio) {
	fdata->f = fopen(filename, fdata->append ? "a+" : "w");
	if(fdata->f == NULL)
		return -1;

	struct stat st = {0};
	if (!fdata->append || fstat(fileno(fdata->f), &st)!=0 || st.st_size == 0) {
		log(LOG_INFO, "Writing to %s\n", filename);
		return 0;
	}
	log(LOG_INFO, "Appending from pos %llu to %s\n", (unsigned long long)st.st_size, filename);

	if(is_audio) {
		//fill missing space with marker tones
		LameTone lt_a(mixmode, 120, 2222);
		LameTone lt_b(mixmode, 120, 1111);
		LameTone lt_c(mixmode, 120, 555);

		int r = lt_a.write(fdata->f);
		if (r==0) r = lt_b.write(fdata->f);
		if (r==0) r = lt_c.write(fdata->f);
		if (fdata->continuous) {
			time_t now = time(NULL);
			if (now > st.st_mtime ) {
				time_t delta = now - st.st_mtime;
				if (delta > 3600) {
					log(LOG_WARNING, "Too big time difference: %llu sec, limiting to one hour\n", (unsigned long long)delta);
					delta = 3600;
				}
				LameTone lt_silence(mixmode, 1000);
				for (; (r==0 && delta > 1); --delta)
					r = lt_silence.write(fdata->f);
			}
		}
		if (r==0) r = lt_c.write(fdata->f);
		if (r==0) r = lt_b.write(fdata->f);
		if (r==0) r = lt_a.write(fdata->f);

		if (r<0) fseek(fdata->f, st.st_size, SEEK_SET);
	}
	return 0;
}

unsigned char lamebuf[LAMEBUF_SIZE];
int16_t iq_buf[2 * WAVE_BATCH];
void process_outputs(channel_t *channel, int cur_scan_freq) {
	int mp3_bytes = 0;
	if(channel->need_mp3) {
		debug_bulk_print("channel->mode=%s\n", channel->mode == MM_STEREO ? "MM_STEREO" : "MM_MONO");
		mp3_bytes = lame_encode_buffer_ieee_float(
			channel->lame,
			channel->waveout,
			(channel->mode == MM_STEREO ? channel->waveout_r : NULL),
			WAVE_BATCH,
			lamebuf,
			LAMEBUF_SIZE
		);
// FIXME: we should not return here, because there might be some non-mp3 outputs
// which can be handled even if MP3 encoder has errored.
		if (mp3_bytes < 0) {
			log(LOG_WARNING, "lame_encode_buffer_ieee_float: %d\n", mp3_bytes);
			return;
		} else if (mp3_bytes == 0)
			return;
	}
	for (int k = 0; k < channel->output_count; k++) {
		if(channel->outputs[k].enabled == false) continue;
		if(channel->outputs[k].type == O_ICECAST) {
			icecast_data *icecast = (icecast_data *)(channel->outputs[k].data);
			if(icecast->shout == NULL) continue;
			int ret = shout_send(icecast->shout, lamebuf, mp3_bytes);
			if (ret != SHOUTERR_SUCCESS || shout_queuelen(icecast->shout) > MAX_SHOUT_QUEUELEN) {
				if (shout_queuelen(icecast->shout) > MAX_SHOUT_QUEUELEN)
					log(LOG_WARNING, "Exceeded max backlog for %s:%d/%s, disconnecting\n",
						icecast->hostname, icecast->port, icecast->mountpoint);
				// reset connection
				log(LOG_WARNING, "Lost connection to %s:%d/%s\n",
					icecast->hostname, icecast->port, icecast->mountpoint);
				shout_close(icecast->shout);
				shout_free(icecast->shout);
				icecast->shout = NULL;
			} else if(icecast->send_scan_freq_tags && cur_scan_freq >= 0) {
				shout_metadata_t *meta = shout_metadata_new();
				char description[32];
				if(channel->freqlist[channel->freq_idx].label != NULL)
					shout_metadata_add(meta, "song", channel->freqlist[channel->freq_idx].label);
				else {
					snprintf(description, sizeof(description), "%.3f MHz", channel->freqlist[channel->freq_idx].frequency / 1000000.0);
					shout_metadata_add(meta, "song", description);
				}
				shout_set_metadata(icecast->shout, meta);
				shout_metadata_free(meta);
			}
		} else if(channel->outputs[k].type == O_FILE || channel->outputs[k].type == O_RAWFILE) {
			file_data *fdata = (file_data *)(channel->outputs[k].data);
			if(fdata->continuous == false && channel->axcindicate == ' ' && channel->outputs[k].active == false) continue;
			time_t t = time(NULL);
			struct tm *tmp;
			if(use_localtime)
				tmp = localtime(&t);
			else
				tmp = gmtime(&t);

			char suffix[32];
			if(strftime(suffix, sizeof(suffix),
				channel->outputs[k].type == O_FILE ? "_%Y%m%d_%H.mp3" : "_%Y%m%d_%H.cs16",
			tmp) == 0) {
				log(LOG_NOTICE, "strftime returned 0\n");
				continue;
			}
			if(fdata->suffix == NULL || strcmp(suffix, fdata->suffix)) {	// need to open new file
				fdata->suffix = strdup(suffix);
				char *filename = (char *)XCALLOC(1, strlen(fdata->dir) + strlen(fdata->prefix) + strlen(fdata->suffix) + 2);
				sprintf(filename, "%s/%s%s", fdata->dir, fdata->prefix, fdata->suffix);
				if(fdata->f != NULL) {
					//todo: finalize file stream with lame_encode_flush_nogap
					fclose(fdata->f);
					fdata->f = NULL;
				}
				int r = fdata_open(fdata, filename, channel->mode, (channel->outputs[k].type == O_RAWFILE ? 0 : 1));
				if (r<0) {
					log(LOG_WARNING, "Cannot open output file %s (%s), output disabled\n", filename, strerror(errno));
					channel->outputs[k].enabled = false;
					free(filename);
					continue;
				}
				free(filename);
			}
			size_t buflen = 0, written = 0;
			void *dataptr = NULL;
			if(channel->outputs[k].type == O_FILE) {
				dataptr = lamebuf;
				buflen = (size_t)mp3_bytes;
			} else if(channel->outputs[k].type == O_RAWFILE) {
				dataptr = iq_buf;
				buflen = 2 * WAVE_BATCH * sizeof(int16_t);
				for(size_t k = 0; k < 2 * WAVE_BATCH; iq_buf[k] = (int16_t)(channel->iq_out[k]), k++)
					;
			}
			written = fwrite(dataptr, 1, buflen, fdata->f);
			if(written < buflen) {
				if(ferror(fdata->f))
					log(LOG_WARNING, "Cannot write to %s/%s%s (%s), output disabled\n",
						fdata->dir, fdata->prefix, fdata->suffix, strerror(errno));
				else
					log(LOG_WARNING, "Short write on %s/%s%s, output disabled\n",
						fdata->dir, fdata->prefix, fdata->suffix);
				fclose(fdata->f);
				fdata->f = NULL;
				channel->outputs[k].enabled = false;
			}
			channel->outputs[k].active = (channel->axcindicate != ' ');
		} else if(channel->outputs[k].type == O_MIXER) {
			mixer_data *mdata = (mixer_data *)(channel->outputs[k].data);
			mixer_put_samples(mdata->mixer, mdata->input, channel->waveout, WAVE_BATCH);
#ifdef PULSE
		} else if(channel->outputs[k].type == O_PULSE) {
			pulse_data *pdata = (pulse_data *)(channel->outputs[k].data);
			if(pdata->continuous == false && channel->axcindicate == ' ')
				continue;

			pulse_write_stream(pdata, channel->mode, channel->waveout, channel->waveout_r, (size_t)WAVE_BATCH * sizeof(float));
#endif
		}
	}
}

void disable_channel_outputs(channel_t *channel) {
	for (int k = 0; k < channel->output_count; k++) {
		output_t *output = channel->outputs + k;
		output->enabled = false;
		if(output->type == O_ICECAST) {
			icecast_data *icecast = (icecast_data *)(channel->outputs[k].data);
			if(icecast->shout == NULL) continue;
			log(LOG_WARNING, "Closing connection to %s:%d/%s\n",
				icecast->hostname, icecast->port, icecast->mountpoint);
			shout_close(icecast->shout);
			shout_free(icecast->shout);
			icecast->shout = NULL;
		} else if(output->type == O_FILE) {
			file_data *fdata = (file_data *)(channel->outputs[k].data);
			if(fdata->f == NULL) continue;
			//todo: finalize file stream with lame_encode_flush_nogap
			fclose(fdata->f);
			fdata->f = NULL;
		} else if(output->type == O_MIXER) {
			mixer_data *mdata = (mixer_data *)(output->data);
			mixer_disable_input(mdata->mixer, mdata->input);
#ifdef PULSE
		} else if(output->type == O_PULSE) {
			pulse_data *pdata = (pulse_data *)(output->data);
			pulse_shutdown(pdata);
#endif
		}
	}
}

void disable_device_outputs(device_t *dev) {
	for(int j = 0; j < dev->channel_count; j++) {
		disable_channel_outputs(dev->channels + j);
	}
}

void* output_thread(void* params) {
	struct freq_tag tag;
	struct timeval tv;
	int new_freq = -1;
	struct timeval ts, te;

	if(DEBUG) gettimeofday(&ts, NULL);
	while (!do_exit) {
		safe_cond_wait(&mp3_cond, &mp3_mutex);
		for (int i = 0; i < mixer_count; i++) {
			if(mixers[i].enabled == false) continue;
			channel_t *channel = &mixers[i].channel;
			if(channel->state == CH_READY) {
				process_outputs(channel, -1);
				channel->state = CH_DIRTY;
			}
		}
		if(DEBUG) {
			gettimeofday(&te, NULL);
			debug_bulk_print("mixeroutput: %lu.%lu %lu\n", te.tv_sec, te.tv_usec, (te.tv_sec - ts.tv_sec) * 1000000UL + te.tv_usec - ts.tv_usec);
			ts.tv_sec = te.tv_sec;
			ts.tv_usec = te.tv_usec;
		}
		for (int i = 0; i < device_count; i++) {
			device_t* dev = devices + i;
			if (dev->input->state == INPUT_RUNNING && dev->waveavail) {
				dev->waveavail = 0;
				if(dev->mode == R_SCAN) {
					tag_queue_get(dev, &tag);
					if(tag.freq >= 0) {
						tag.tv.tv_sec += shout_metadata_delay;
						gettimeofday(&tv, NULL);
						if(tag.tv.tv_sec < tv.tv_sec || (tag.tv.tv_sec == tv.tv_sec && tag.tv.tv_usec <= tv.tv_usec)) {
							new_freq = tag.freq;
							tag_queue_advance(dev);
						}
					}
				}
				for (int j = 0; j < dev->channel_count; j++) {
					channel_t* channel = devices[i].channels + j;
					process_outputs(channel, new_freq);
					memcpy(channel->waveout, channel->waveout + WAVE_BATCH, AGC_EXTRA * 4);
				}
			}
// make sure we don't carry new_freq value to the next receiver which might be working
// in multichannel mode
			new_freq = -1;
		}
	}
	return 0;
}

// reconnect as required
void* output_check_thread(void* params) {
	while (!do_exit) {
		SLEEP(10000);
		for (int i = 0; i < device_count; i++) {
			device_t* dev = devices + i;
			for (int j = 0; j < dev->channel_count; j++) {
				for (int k = 0; k < dev->channels[j].output_count; k++) {
					if(dev->channels[j].outputs[k].type == O_ICECAST) {
						icecast_data *icecast = (icecast_data *)(dev->channels[j].outputs[k].data);
						if(dev->input->state == INPUT_FAILED) {
							if(icecast->shout) {
								log(LOG_WARNING, "Device #%d failed, disconnecting stream %s:%d/%s\n",
									i, icecast->hostname, icecast->port, icecast->mountpoint);
								shout_close(icecast->shout);
								shout_free(icecast->shout);
								icecast->shout = NULL;
							}
						} else if(dev->input->state == INPUT_RUNNING) {
							if (icecast->shout == NULL){
								log(LOG_NOTICE, "Trying to reconnect to %s:%d/%s...\n",
									icecast->hostname, icecast->port, icecast->mountpoint);
								shout_setup(icecast, dev->channels[j].mode);
							}
						}
#ifdef PULSE
					} else if(dev->channels[j].outputs[k].type == O_PULSE) {
						pulse_data *pdata = (pulse_data *)(dev->channels[j].outputs[k].data);
						if(dev->input->state == INPUT_FAILED) {
							if(pdata->context) {
								pulse_shutdown(pdata);
							}
						} else if(dev->input->state == INPUT_RUNNING) {
							if (pdata->context == NULL){
								pulse_setup(pdata, dev->channels[j].mode);
							}
						}
#endif
					}
				}
			}
		}
		for (int i = 0; i < mixer_count; i++) {
			if(mixers[i].enabled == false) continue;
			for (int k = 0; k < mixers[i].channel.output_count; k++) {
				if(mixers[i].channel.outputs[k].enabled == false)
					continue;
				if(mixers[i].channel.outputs[k].type == O_ICECAST) {
					icecast_data *icecast = (icecast_data *)(mixers[i].channel.outputs[k].data);
					if(icecast->shout == NULL) {
						log(LOG_NOTICE, "Trying to reconnect to %s:%d/%s...\n",
							icecast->hostname, icecast->port, icecast->mountpoint);
						shout_setup(icecast, mixers[i].channel.mode);
					}
#ifdef PULSE
				} else if(mixers[i].channel.outputs[k].type == O_PULSE) {
					pulse_data *pdata = (pulse_data *)(mixers[i].channel.outputs[k].data);
					if (pdata->context == NULL){
						pulse_setup(pdata, mixers[i].channel.mode);
					}
#endif
				}
			}
		}
	}
	return 0;
}

// vim: ts=4
