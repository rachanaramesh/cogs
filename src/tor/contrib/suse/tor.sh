#!/bin/sh
#
# Copyright (c) 2006-2007 Andrew Lewman
#
# tor    The Onion Router
#
# Startup/shutdown script for tor. This is a wrapper around torctl;
# torctl does the actual work in a relatively system-independent, or at least
# distribution-independent, way, and this script deals with fitting the
# whole thing into the conventions of the particular system at hand.
#
# These next couple of lines "declare" tor for the "chkconfig" program,
# originally from SGI, used on Red Hat/Fedora and probably elsewhere.
#
# chkconfig: 2345 90 10
# description: Onion Router - A low-latency anonymous proxy
#

### BEGIN INIT INFO
# Provides: tor
# Required-Start: $remote_fs $network
# Required-Stop: $remote_fs $network
# Default-Start: 3 5
# Default-Stop: 0 1 2 6
# Short-Description: Start the tor daemon
# Description:  Start the tor daemon:  the anon-proxy server
### END INIT INFO

. /etc/rc.status

# Shell functions sourced from /etc/rc.status:
#      rc_check         check and set local and overall rc status
#      rc_status        check and set local and overall rc status
#      rc_status -v     ditto but be verbose in local rc status
#      rc_status -v -r  ditto and clear the local rc status
#      rc_failed        set local and overall rc status to failed
#      rc_reset         clear local rc status (overall remains)
#      rc_exit          exit appropriate to overall rc status

# First reset status of this service
rc_reset

# Increase open file descriptors a reasonable amount
ulimit -n 8192

TORCTL=/usr/local/bin/torctl

# torctl will use these environment variables
TORUSER=_tor
export TORUSER
TORGROUP=_tor
export TORGROUP

TOR_DAEMON_PID_DIR="/usr/local/var/run/tor"

if [ -x /bin/su ] ; then
    SUPROG=/bin/su
elif [ -x /sbin/su ] ; then
    SUPROG=/sbin/su
elif [ -x /usr/bin/su ] ; then
    SUPROG=/usr/bin/su
elif [ -x /usr/sbin/su ] ; then
    SUPROG=/usr/sbin/su
else
    SUPROG=/bin/su
fi

case "$1" in

    start)
    echo "Starting tor daemon"

    if [ ! -d $TOR_DAEMON_PID_DIR ] ; then
        mkdir -p $TOR_DAEMON_PID_DIR
        chown $TORUSER:$TORGROUP $TOR_DAEMON_PID_DIR
    fi

    ## Start daemon with startproc(8). If this fails
    ## the echo return value is set appropriate.

    startproc -f $TORCTL start
    # Remember status and be verbose
    rc_status -v
    ;;

    stop)
    echo "Stopping tor daemon" 
    startproc -f $TORCTL stop
    # Remember status and be verbose
    rc_status -v
    ;;

    restart)
    echo "Restarting tor daemon" 
    startproc -f $TORCTL restart
    # Remember status and be verbose
    rc_status -v
    ;;

    reload)
    echo "Reloading tor daemon" 
    startproc -f $TORCTL reload
    # Remember status and be verbose
    rc_status -v
    ;;

    status)
    startproc -f $TORCTL status
    # Remember status and be verbose
    rc_status -v
    ;;

    *)
    echo "Usage: $0 (start|stop|restart|reload|status)"
    RETVAL=1
esac

rc_exit
