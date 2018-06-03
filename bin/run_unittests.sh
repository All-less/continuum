#!/usr/bin/env bash

set -e
set -u
set -o pipefail

function usage {
    cat <<EOF
    usage: run_unittests.sh

    This script is used to run Continuum tests. By default, it will run all possible
    tests

    Options:

    -a, --all                   Run all tests
    -l, --libcontinuum          Run tests only for libcontinuum folder.
    -m, --management            Run tests only for management folder.
    -f, --frontend              Run tests only for frontend folder.
    -h, --help                  Display this message and exit.

$@
EOF
}

function clean_up {
    # Perform program exit housekeeping
    # echo Background jobs: $(jobs -l)
    # echo
    # echo Killing jobs
    echo Exiting unit tests...
    kill $(jobs -p) &> /dev/null
    echo
    sleep 2
    # echo Remaining background jobs: $(jobs -l)
    exit
}

function randomize_redis_port {
    set +e  # turn of exit on command fail
    REDIS_PORT=$((34256 + RANDOM % 1000))
    lsof -i :$REDIS_PORT &> /dev/null

    if [ $? -eq 0 ]; then # existing port in use found
      while true; do
        REDIS_PORT=$(($REDIS_PORT + RANDOM % 1000))
        lsof -i :$REDIS_PORT &> /dev/null
        if [ $? -eq 1 ]; then  # port not in use
          break
        fi
      done
    fi
    echo "$REDIS_PORT"
}

trap clean_up SIGHUP SIGINT SIGTERM EXIT

unset CDPATH
# one-liner from http://stackoverflow.com/a/246128
# Determines absolute path of the directory containing
# the script.
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

function set_test_environment {
  # Let the user start this script from anywhere in the filesystem.
  cd $DIR/..
  ./configure
  cd debug
  # make all to make sure all the binaries compile
  make -j2 all unittests
  if ! type "redis-server" &> /dev/null; then
      echo -e "\nERROR:"
      echo -e "\tUnit tests require Redis. Please install redis-server"
      echo -e "\tand make sure it's on your PATH.\n"
      exit 1
  fi

  randomize_redis_port
  set -e # turn back on exit on command fail

  # start Redis on the test port if it's not already running
  redis-server --port $REDIS_PORT &> /dev/null &
}

function run_libcontinuum_tests {
  cd $DIR/../debug
  echo -e "\nRunning libcontinuum tests\n\n"
  ./src/libcontinuum/libcontinuumtests --redis_port $REDIS_PORT
}

function run_management_tests {
  cd $DIR/../debug
  echo -e "\nRunning management tests\n\n"
  ./src/management/managementtests --redis_port $REDIS_PORT
}

function run_frontend_tests {
  cd $DIR/../debug
  echo -e "\nRunning frontend tests\n\n"
  ./src/frontends/frontendtests --redis_port $REDIS_PORT
}

function run_all_tests {
  run_libcontinuum_tests
  redis-cli -p $REDIS_PORT "flushall"
  run_frontend_tests
  redis-cli -p $REDIS_PORT "flushall"
  run_management_tests
  redis-cli -p $REDIS_PORT "flushall"
}

if [ "$#" == 0 ]
then
  args="--all"
else
  args=$1
fi

case $args in
    -a | --all )            set_test_environment
                            run_all_tests
                            ;;
    -l | --libcontinuum )   set_test_environment
                            run_libcontinuum_tests
                            ;;
    -m | --management )     set_test_environment
                            run_management_tests
                            ;;
    -f | --frontend )       set_test_environment
                            run_frontend_tests
                            ;;
    -h | --help )           usage
                            ;;
    * )                     usage
esac
