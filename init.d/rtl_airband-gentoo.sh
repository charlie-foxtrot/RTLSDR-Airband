#!/sbin/runscript
# rtl_airband Gentoo startup script
# (c) 2015 Tomasz Lemiech <szpajder@gmail.com>

RTLAIRBAND_CONFDIR=${RTLAIRBAND_CONFDIR:-/usr/local/etc}
RTLAIRBAND_CONFIG=${RTLAIRBAND_CONFIG:-${RTLAIRBAND_CONFDIR}/rtl_airband.conf}
RTLAIRBAND_PIDFILE=${RTLAIRBAND_PIDFILE:-/run/${SVCNAME}.pid}
RTLAIRBAND_BINARY=${RTLAIRBAND_BINARY:-/usr/local/bin/rtl_airband}

depend() {
	use logger dns
}

checkconfig() {
	if [ ! -e "${RTLAIRBAND_CONFIG}" ] ; then
		eerror "You need an ${RTLAIRBAND_CONFIG} file to run rtl_airband"
		return 1
	fi
}

start() {
	checkconfig || return 1

	ebegin "Starting ${SVCNAME}"
	start-stop-daemon --start --exec "${RTLAIRBAND_BINARY}" \
	    --pidfile "${RTLAIRBAND_PIDFILE}" \
	    -- ${RTLAIRBAND_OPTS}
	eend $?
}

stop() {
	if [ "${RC_CMD}" = "restart" ] ; then
		checkconfig || return 1
	fi

	ebegin "Stopping ${SVCNAME}"
	start-stop-daemon --stop --exec "${RTLAIRBAND_BINARY}" \
	    --pidfile "${RTLAIRBAND_PIDFILE}" --quiet
	eend $?
}
