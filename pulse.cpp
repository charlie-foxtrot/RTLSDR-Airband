/*
 * pulse.cpp
 * PulseAudio output routines
 *
 * Copyright (c) 2015-2017 Tomasz Lemiech <szpajder@gmail.com>
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

#include <iostream>
#include <syslog.h>
#include <pulse/pulseaudio.h>
#include "rtl_airband.h"

#define COALESCE(x) ((x) ? (x) : "<unspecified>")
#define PA_LOOP_LOCK(x) if(!pa_threaded_mainloop_in_thread(x)) { pa_threaded_mainloop_lock(x); }
#define PA_LOOP_UNLOCK(x) if(!pa_threaded_mainloop_in_thread(x)) { pa_threaded_mainloop_unlock(x); }

using namespace std;

/* typedef struct {
	const char *server;
	pa_context *pulse_context;
	pa_context_list *next;
} pa_context_list; */

pa_threaded_mainloop *mainloop = NULL;

void pulse_shutdown(pulse_data *pdata) {
	PA_LOOP_LOCK(mainloop);
	if(!pdata)
		return;
	if(pdata->left) {
		pa_stream_disconnect(pdata->left);
		pa_stream_unref(pdata->left);
		pdata->left = NULL;
	}
	if(pdata->right) {
		pa_stream_disconnect(pdata->right);
		pa_stream_unref(pdata->right);
		pdata->right = NULL;
	}
	if(pdata->context) {
		pa_context_disconnect(pdata->context);
		pa_context_unref(pdata->context);
		pdata->context = NULL;
	}
	PA_LOOP_UNLOCK(mainloop);
}

static void pulse_stream_underflow_cb(pa_stream *stream, void *userdata) {
	pulse_data *pdata = (pulse_data *)userdata;
	log(LOG_INFO, "pulse: server %s: stream \"%s\": underflow occurred\n", COALESCE(pdata->server), pdata->stream_name);
}

static void pulse_stream_overflow_cb(pa_stream *stream, void *userdata) {
	pulse_data *pdata = (pulse_data *)userdata;
	log(LOG_INFO, "pulse: server %s: stream \"%s\": overflow occurred\n", COALESCE(pdata->server), pdata->stream_name);
}

static void stream_state_cb(pa_stream *stream, void *userdata) {
	pulse_data *pdata = (pulse_data *)userdata;

	switch (pa_stream_get_state(stream)) {
	case PA_STREAM_READY:
	case PA_STREAM_UNCONNECTED:
	case PA_STREAM_CREATING:
		break;
	case PA_STREAM_FAILED:
	case PA_STREAM_TERMINATED:
		log(LOG_INFO, "pulse: stream to %s changed state to failed or terminated", COALESCE(pdata->server));
// pulse_shutdown?
		break;
	break;
	}
}

static void pulse_setup_streams(pulse_data *pdata) {
	static const pa_sample_spec ss = {
		.format = PA_SAMPLE_FLOAT32LE,
		.rate = WAVE_RATE,
		.channels = 1,
	};
	PA_LOOP_LOCK(mainloop);
	if(!(pdata->left = pa_stream_new(pdata->context, pdata->stream_name, &ss, NULL))) {
		log(LOG_ERR, "pulse: failed to create stream %s on server %s: %s",
			pdata->stream_name, COALESCE(pdata->server), pa_strerror(pa_context_errno(pdata->context)));
		goto fail;
	}
	pa_stream_set_state_callback(pdata->left, stream_state_cb, pdata);
	pa_stream_set_underflow_callback(pdata->left, pulse_stream_underflow_cb, pdata);
	pa_stream_set_overflow_callback(pdata->left, pulse_stream_overflow_cb, pdata);
//	pa_stream_set_write_callback(pdata->left, stream_request_cb, pdata);
	if(pa_stream_connect_playback(pdata->left, NULL, NULL,
				      (pa_stream_flags_t)(PA_STREAM_INTERPOLATE_TIMING
				      |PA_STREAM_ADJUST_LATENCY
				      |PA_STREAM_AUTO_TIMING_UPDATE), NULL, NULL) < 0) {
		log(LOG_ERR, "pulse: failed to connect playback stream %s on server %s: %s",
			pdata->stream_name, COALESCE(pdata->server), pa_strerror(pa_context_errno(pdata->context)));
		goto fail;
	}
	log(LOG_INFO, "pulse: playback stream connected to server %s", COALESCE(pdata->server));
	PA_LOOP_UNLOCK(mainloop);
	return;
fail:
	pulse_shutdown(pdata);
}

static void pulse_ctx_state_cb(pa_context *c, void *userdata) {
	pulse_data *pdata = (pulse_data *)userdata;
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
		log(LOG_INFO, "pulse: connected to %s", COALESCE(pdata->server));
		pulse_setup_streams(pdata);
		break;
	case PA_CONTEXT_TERMINATED:
		log(LOG_INFO, "pulse: connection to %s terminated", COALESCE(pdata->server));
// pulse_shutdown?
		break;
	case PA_CONTEXT_FAILED:
		log(LOG_ERR, "pulse: connection to %s failed: %s", COALESCE(pdata->server), pa_strerror(pa_context_errno(pdata->context)));
		pulse_shutdown(pdata);
		break;
	case PA_CONTEXT_CONNECTING:
		log(LOG_INFO, "pulse: connecting to %s...", COALESCE(pdata->server));
		break;
	case PA_CONTEXT_UNCONNECTED:
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_SETTING_NAME:
		break;
	}
}

void pulse_init() {
	if(!mainloop && !(mainloop = pa_threaded_mainloop_new())) {
		cerr<<"Failed to initialize PulseAudio main loop - aborting\n";
		error();
	}
}

int pulse_setup(pulse_data *pdata, mix_modes mixmode) {
	if(!(pdata->context = pa_context_new(pa_threaded_mainloop_get_api(mainloop), "rtl_airband"))) {
		log(LOG_ERR, "%s" "Failed to create PulseAudio context\n");
		return -1;
	}
	pa_context_set_state_callback(pdata->context, &pulse_ctx_state_cb, pdata);
	if(pa_context_connect(pdata->context, pdata->server, PA_CONTEXT_NOFLAGS, NULL) < 0) {
		log(LOG_ERR, "pulse: failed to connect to %s: %s", COALESCE(pdata->server), pa_strerror(pa_context_errno(pdata->context)));
		pa_context_unref(pdata->context);
		pdata->context = NULL;
		return -1;
	}
	return 0;
}

void pulse_start() {
	if(!mainloop) return;
	PA_LOOP_LOCK(mainloop);
	pa_threaded_mainloop_start(mainloop);
	PA_LOOP_UNLOCK(mainloop);
}

void pulse_write_stream(pulse_data *pdata, mix_modes mode, float *data_left, float *data_right, size_t len) {
	PA_LOOP_LOCK(mainloop);
	if(!pdata->context || pa_context_get_state(pdata->context) != PA_CONTEXT_READY)
		goto end;
	if(!pdata->left || pa_stream_get_state(pdata->left) != PA_STREAM_READY)
		goto end;

        if(pa_stream_write(pdata->left, data_left, len, NULL, 0LL, PA_SEEK_RELATIVE) < 0) {
		log(LOG_NOTICE, "pa_stream_write failed, disconnecting server %s\n", COALESCE(pdata->server));
		pulse_shutdown(pdata);
		goto end;
	}
//TODO: handle stereo stream

end:
	PA_LOOP_UNLOCK(mainloop);
	return;
}
