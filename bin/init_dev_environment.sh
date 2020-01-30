#!/usr/bin/env bash
set -euo pipefail


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJ_ROOT=$DIR/..
cd $PROJ_ROOT

function usage {
    cat <<EOF
    usage: init_dev_environment.sh

    This script is used to install Continuum dependencies. By default, it will fully
    bootstrap a development environment.

    Options:

    -a, --all                   Install all dependencies.
    -s, --system                Install system dependencies.
    -l, --library               Install third-party libraries.
    -b, --build                 Build Continuum.
    -h, --help                  Display this message and exit.

$@
EOF
}

function ensure_non_empty {
    if [[ -z "$(ls -A $1)" ]]; then
        echo "Error: $1 is empty."
        exit 1
    fi
}

function install_system_dependencies {
    sudo apt update -yq

    # folly dependencies
    sudo apt install -yq \
        g++-6 \
        binutils-dev \
        libdouble-conversion-dev \
        libdwarf-dev \
        libelf-dev \
        libevent-dev \
        libgflags-dev \
        libgoogle-glog-dev \
        libiberty-dev \
        libjemalloc-dev \
        liblz4-dev \
        liblzma-dev \
        libsnappy-dev \
        libssl-dev \
        libunwind8-dev \
        pkg-config \
        zlib1g-dev \
        make

    # ZeroMQ for RPC
    sudo apt install -yq \
        libzmq-jni \
        libzmq3-dev

    # Redis
    sudo apt install -yq redis-server

    # Python for backends
    sudo apt install -yq \
        python-numpy \
        python-zmq \
        python-redis \
        python-pip

    # other build tools
    sudo apt install -yq \
        libtool \
        autoconf \
        autoconf-archive \
        automake \
        cmake \
        git \
        curl

    # We install boost 1.65 because 1.66+ will result in compilation error. More concretely,
    # boost 1.66+ introduces a new class `signal_set`, whereas `signal_set` has been defined
    # as a macro in libevent.
    cd /opt
    curl -L https://dl.bintray.com/boostorg/release/1.65.1/source/boost_1_65_1.tar.gz -o boost_1_65_1.tar.gz
    tar xzvf boost_1_65_1.tar.gz
    cd boost_1_65_1
    ./bootstrap.sh
    ./b2 install --prefix=/usr
}

function install_third_party {
    # patch libev
    LIBEV_DIR=$PROJ_ROOT/src/libs/libev
    ensure_non_empty $LIBEV_DIR
    cd ${LIBEV_DIR}
    git checkout 93823e6ca699df195a6c7b8bfa6006ec40ee0003
    git apply $PROJ_ROOT/src/libs/patches/rename_libev_symbol.patch
    ./configure
    make
    sudo make install

    # patch hiredis
    HIREDIS_DIR=$PROJ_ROOT/src/libs/hiredis
    ensure_non_empty $HIREDIS_DIR
    cd ${HIREDIS_DIR}
    git checkout 3d8709d19d7fa67d203a33c969e69f0f1a4eab02
    git apply $PROJ_ROOT/src/libs/patches/rename_hiredis_symbol.patch
    make
    sudo make install

    # patch redox
    REDOX_DIR=$PROJ_ROOT/src/libs/redox
    ensure_non_empty $REDOX_DIR
    cd ${REDOX_DIR}
    git checkout 2272f30f18890a6c18632ac3a862b0a879489b40
    git apply $PROJ_ROOT/src/libs/patches/rename_redox_symbol.patch

    # download folly
    cd $PROJ_ROOT/src/libs
    git clone https://github.com/facebook/folly.git
    cd $PROJ_ROOT/src/libs/folly
    git checkout 62e3abb2f1ec5c5041a4aa0fa7744ae46f6d25d7
    cd $PROJ_ROOT/src/libs/folly/folly

    autoreconf -ivf
    ./configure
    make
    sudo make install
}



function build_project {
    # build project
    cd $PROJ_ROOT
    ./configure
    cd debug/
    make

    # install backend dependencies
    cd $PROJ_ROOT/backends/python
    pip install -r requirements.txt
}

function install_all {
    install_system_dependencies
    install_third_party
    build_project
}

if [[ "$#" == 0 ]]; then
  ARGS="--all"
else
  ARGS=$1
fi

case $ARGS in
    -a | --all )     install_all
                     ;;
    -s | --system )  install_system_dependencies
                     ;;
    -l | --library ) install_third_party
                     ;;
    -b | --build )   build_project
                     ;;
    * )              usage
esac
