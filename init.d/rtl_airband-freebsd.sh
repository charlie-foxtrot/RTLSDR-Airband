#!/bin/sh

# PROVIDE: rtl_airband
# REQUIRE: DAEMON
# BEFORE: LOGIN
# KEYWORD: nojail shutdown

. /etc/rc.subr

name=rtl_airband
rcvar=rtl_airband_enable

command="/usr/local/bin/rtl_airband"

load_rc_config ${name}
run_rc_command "$1"
