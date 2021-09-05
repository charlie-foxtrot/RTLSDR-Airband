#include <string.h>		// strerror()
#include <unistd.h>		// close()
#include <syslog.h>		// LOG_INFO / LOG_ERR

#include <arpa/inet.h>	// inet_aton()

#include "rtl_airband.h"

bool udp_stream_init(udp_stream_data *sdata, mix_modes mode) {
	// Create the send socket
	sdata->send_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sdata->send_socket == -1) {
		log(LOG_ERR, "udp_stream: socket failed: %s\n", strerror(errno));
		return false;
	}

	// Setup the address (ip / port) to send to
	sdata->dest_addr_len = sizeof(sdata->dest_addr);
	memset((char *) &sdata->dest_addr, 0, sdata->dest_addr_len);

	sdata->dest_addr.sin_family = AF_INET;
	sdata->dest_addr.sin_port = htons(sdata->dest_port);

	if (inet_aton(sdata->dest_ip, &sdata->dest_addr.sin_addr) == 0) {
		log(LOG_ERR, "udp_stream: inet_aton: invalid IPv4 address \"%s\"\n", sdata->dest_ip);
		return false;
	}

	log(LOG_INFO, "udp_stream: sending %s 32-bit float at %d Hz to %s:%d\n", mode==MM_MONO ? "Mono" : "Stereo", WAVE_RATE, sdata->dest_ip, sdata->dest_port);
	return true;
}

void udp_stream_write(udp_stream_data *sdata, float *data, size_t len) {
	if (sdata->send_socket != -1) {
		// Send without blocking or checking for success
		sendto(sdata->send_socket, data, len, MSG_DONTWAIT | MSG_NOSIGNAL, (struct sockaddr *)&(sdata->dest_addr), sdata->dest_addr_len);
	}
}

void udp_stream_write(udp_stream_data *sdata, float *data_left, float *data_right, size_t len) {
	if (sdata->send_socket != -1) {
		float *interleaved = (float *)XCALLOC(len * 2, sizeof(float));
		for (size_t i = 0; i < len; ++i) {
			interleaved[2*i] = data_left[i];
			interleaved[2*i + 1] = data_right[i];
		}
		udp_stream_write(sdata, interleaved, len * 2);
	}
}

void udp_stream_shutdown(udp_stream_data *sdata) {
	if (sdata->send_socket != -1) {
		close(sdata->send_socket);
	}
}
