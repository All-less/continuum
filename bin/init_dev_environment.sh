#!/usr/bin/env bash
set -euo pipefail


DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJ_ROOT=$DIR/..
cd $PROJ_ROOT

# install dependencies
sudo apt update -yq
sudo apt install -yq autoconf autoconf-archive automake binutils-dev cmake g++-6 \
    git libboost-all-dev libdouble-conversion-dev libdwarf-dev libelf-dev libevent-dev \
    libgflags-dev libgoogle-glog-dev libiberty-dev libjemalloc-dev liblz4-dev liblzma-dev \
    libsnappy-dev libssl-dev libtool libunwind8-dev libzmq-jni libzmq3-dev make maven \
    openjdk-8-jdk pkg-config python-numpy python-zmq python-redis redis-server zlib1g-dev \
    python-pip curl
echo 'export JZMQ_HOME=/usr/lib/x86_64-linux-gnu/jni' >> ~/.bashrc

# patch libev
cd $PROJ_ROOT/src/libs/libev
git checkout 93823e6ca699df195a6c7b8bfa6006ec40ee0003
git am --signoff < $PROJ_ROOT/src/libs/patches/rename_libev_symbol.patch
./configure
make
sudo make install

# patch hiredis
cd $PROJ_ROOT/src/libs/hiredis
git checkout 3d8709d19d7fa67d203a33c969e69f0f1a4eab02
git am --signoff < $PROJ_ROOT/src/libs/patches/rename_hiredis_symbol.patch
make
sudo make install

# patch redox
cd $PROJ_ROOT/src/libs/redox
git checkout 2272f30f18890a6c18632ac3a862b0a879489b40
git am --signoff < $PROJ_ROOT/src/libs/patches/rename_redox_symbol.patch

# download folly
cd /vagrant/src/libs
git clone https://github.com/facebook/folly.git
cd /vagrant/src/libs/folly
git checkout 62e3abb2f1ec5c5041a4aa0fa7744ae46f6d25d7
cd /vagrant/src/libs/folly/folly

autoreconf -ivf
./configure
make
sudo make install

# build project
cd $PROJ_ROOT
./configure
cd debug/
make

# install backend dependencies
cd $PROJ_ROOT/backends/python
pip install -r requirements.txt

${PROJ_ROOT}/bin/run_unittests.sh
