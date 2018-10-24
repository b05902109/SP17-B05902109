#!/bin/sh

# set bash fail at subcommand
#set -e
BOXDIR=$(cd "$(dirname $0)/.." && pwd)
SCRIPT="$BOXDIR/script/csiebox_server.sh"
DAEMON="$BOXDIR/bin/csiebox_server"
CONFIG="$BOXDIR/config/server.cfg"
RUNPATH=$(grep "run_path" $CONFIG | sed 's/.*=//g')
PIDFILE="$RUNPATH/csiebox_server.pid"

if [ -r /etc/default/locale ]; then
  . /etc/default/locale
  export LANG LANGUAGE
fi

if [ ! -d $RUNPATH ] || [ -z $RUNPATH ]; then
  echo "run_path is not a directory"
  exit 0
fi

if [ ! -x $DAEMON ]; then
  echo "cannot find excutable"
  exit 0
fi

. lib/lsb/init-functions

START_ARGS="--pidfile $PIDFILE --make-pidfile --name $(basename $DAEMON) --startas $DAEMON -- $CONFIG -d"
STOP_ARGS="--pidfile $PIDFILE --name $(basename $DAEMON) --retry TERM/5/TERM/5"

case "$1" in
  start)
    log_daemon_msg "Starting" "CSIEBOX_SERVER"
    ./start-stop-daemon --start  --quiet $START_ARGS

    RES=$?
    if [ $RES -eq 1 ]; then
      log_daemon_msg "already running"
    fi
    if [ $RES -ne 0 ]; then
      log_end_msg 1
    else
      log_end_msg 0
    fi
  ;;

  stop)
    log_daemon_msg "Stopping" "CSIEBOX_SERVER"
    if ! [ -f $PIDFILE ]; then
      log_daemon_msg "not running ($PIDFILE not fonud)"
      log_end_msg 1
    else
      ./start-stop-daemon --stop --quiet $STOP_ARGS
      RES=$?
      if [ $RES -eq 1 ]; then
        log_daemon_msg "not running"
        log_end_msg 1
      elif [ $RES -eq 2 ]; then
        log_daemon_msg "not responding to TERM signals"
        log_end_msg 1
      else
        if [ -f $PIDFILE ]; then
          log_daemon_msg "(removing stale $PIDFILE)"
          rm $PIDFILE
        fi
        log_end_msg 0
      fi
    fi
  ;;

  *)
    echo "Usage: $0 {start|stop}"
    exit 1
  ;;
esac

exit 0
