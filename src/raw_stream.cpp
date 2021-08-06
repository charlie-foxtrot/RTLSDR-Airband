#include <string.h>		// strerror()
#include <unistd.h>		// close()
#include <syslog.h>		// LOG_INFO / LOG_ERR

#include <arpa/inet.h>	// inet_aton()

#include "rtl_airband.h"

bool raw_stream_init(raw_stream_data *sdata) {
	// Create the send socket
	sdata->send_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sdata->send_socket == -1) {
		log(LOG_ERR, "raw_stream: socket failed: %s\n", strerror(errno));
		return false;
	}

	// Setup the address (ip / port) to send to
	sdata->dest_addr_len = sizeof(sdata->dest_addr);
	memset((char *) &sdata->dest_addr, 0, sdata->dest_addr_len);

	sdata->dest_addr.sin_family = AF_INET;
	sdata->dest_addr.sin_port = htons(sdata->dest_port);

	if (inet_aton(sdata->dest_ip, &sdata->dest_addr.sin_addr) == 0) {
		log(LOG_ERR, "raw_stream: inet_aton: invalid IPv4 address \"%s\"\n", sdata->dest_ip);
		return false;
	}

	log(LOG_INFO, "raw_stream: sending to %s:%d\n", sdata->dest_ip, sdata->dest_port);
	return true;
}

void raw_stream_write(raw_stream_data *sdata, float *data, size_t len) {
	if (sdata->send_socket != -1) {
		// Send without blocking or checking for success
		sendto(sdata->send_socket, data, len, MSG_DONTWAIT | MSG_NOSIGNAL, (struct sockaddr *)&(sdata->dest_addr), sdata->dest_addr_len);
	}
}

void raw_stream_shutdown(raw_stream_data *sdata) {
	if (sdata->send_socket != -1) {
		close(sdata->send_socket);
	}
}
