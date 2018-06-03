#!/usr/bin/env bash
set -euo pipefail


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Let the user start this script from anywhere in the filesystem.
CONTINUUM_ROOT=$DIR/..
cd $CONTINUUM_ROOT

PID_FILE=${CONTINUUM_ROOT}/debug/pid
STATE_FILE=${CONTINUUM_ROOT}/debug/state
LOG_DIR=${CONTINUUM_ROOT}/debug/logs
mkdir -p ${LOG_DIR}


build_project() {
    cd $CONTINUUM_ROOT/debug
    make
}

start_data_frontend() {
    $CONTINUUM_ROOT/debug/src/frontends/data_frontend >${LOG_DIR}/data_frontend.log 2>&1 &
    DATA_PID="$!"
    echo "data_frontend started! PID : $DATA_PID"
    echo "$DATA_PID " >> ${PID_FILE}
}

start_management_frontend() {
    $CONTINUUM_ROOT/debug/src/management/management_frontend >${LOG_DIR}/management_frontend.log 2>&1 &
    MNGM_PID="$!"
    echo "management_frontend started! PID : $MNGM_PID"
    echo "$MNGM_PID " >> ${PID_FILE}
}

start_redis() {
    if ! type "redis-server" &> /dev/null; then
        echo -e "\nERROR:"
        echo -e "\tContinuum require Redis to run. Please install redis-server"
        echo -e "\tand make sure it's on your PATH.\n"
        exit 1
    fi

    # start Redis if it's not already running
    redis-server &> /dev/null &
    echo "Redis has started."
    sleep 1
}

start_celery() {
    cd $CONTINUUM_ROOT/backends/python
    celery -A modules.worker worker -l info >${LOG_DIR}/celery.log 2>&1 &
    CLRY_PID="$!"
    echo "Celery started! PID : $CLRY_PID"
    echo "$CLRY_PID " >> ${PID_FILE}
}

start_all() {
    if [ ! -f ${STATE_FILE} ]; then
      touch ${STATE_FILE}
    fi

    if [[ `cat ${STATE_FILE}` == "1" ]]; then
        echo "Continuum has started! "
        exit 1
    fi

    build_project

    clean_logs
    mkdir ${LOG_DIR}

    start_redis
    flush_redis

    start_management_frontend
    start_data_frontend
    start_celery

    echo "1" > ${STATE_FILE}
}

stop_all() {
    if [[ ! -f ${STATE_FILE} ]]; then
        touch ${STATE_FILE}
    fi

    if [[ `cat ${STATE_FILE}` != "0" ]]; then
        for PID in `cat ${PID_FILE}`; do
            kill ${PID} || echo "${PID} does not exist."  # avoid non-zero return code
        done
        rm ${PID_FILE}
        echo "0" > ${STATE_FILE}
    else
        echo "Continuum not running!"
    fi
}

clean_logs() {
    if [ -d ${LOG_DIR} ]; then
        rm -rf ${LOG_DIR}
    fi
}

flush_redis() {
    redis-cli flushall
}

start_debug_data_frontend() {
    sleep 1 && curl -X POST \
        --header "Content-Type:application/json" \
        -d '{"name":"test-app", "input_type":"doubles", "default_output":"-1.0", "latency_slo_micros":100000 }' \
        http://0.0.0.0:1338/admin/add_app &

    ${CONTINUUM_ROOT}/debug/src/frontends/data_frontend
}

if [[ "$#" == 0 ]]; then
    echo "Usage: $0 start|stop|restart|clean"
    exit 1
else
    case $1 in
        start)                  start_all
                                ;;
        stop)                   stop_all
                                ;;
        restart)                stop_all
                                start_all
                                ;;
        clean )                 clean_logs
                                ;;
        * )                     echo "Usage: $0 start|stop|restart|clean"
    esac
fi

