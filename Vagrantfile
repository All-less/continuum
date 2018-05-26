# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|

  config.vm.provider "virtualbox" do |vb|
    vb.memory = "4096"
  end

  # As we need to patch redox, we exclude it from sync.
  config.vm.synced_folder ".", "/vagrant", type: "rsync",
    rsync__exclude: [ ".git/", "debug/", "src/libs/redox" ]

  config.vm.box = "debian/testing64"

  # Enable provisioning with a shell script. Additional provisioners such as
  # Puppet, Chef, Ansible, Salt, and Docker are also available. Please see the
  # documentation for more information about their specific syntax and use.
  config.vm.provision "shell", privileged: false, inline: <<-SHELL
    sudo apt-get update
    sudo apt-get upgrade -y
    sudo apt-get install -y autoconf autoconf-archive automake binutils-dev cmake g++ \
        git libboost-all-dev libdouble-conversion-dev libdwarf-dev libelf-dev libevent-dev \
        libgflags-dev libgoogle-glog-dev libiberty-dev libjemalloc-dev liblz4-dev liblzma-dev \
        libsnappy-dev libssl-dev libtool libunwind8-dev libzmq-jni libzmq3-dev make maven \
        openjdk-8-jdk pkg-config python-numpy python-zmq python-redis redis-server zlib1g-dev \
        python-pip curl
    echo 'export JZMQ_HOME=/usr/lib/x86_64-linux-gnu/jni' >> ~/.bashrc

    cd /vagrant/src/libs
    git clone https://github.com/enki/libev
    cd /vagrant/src/libs/libev
    git checkout 93823e6ca699df195a6c7b8bfa6006ec40ee0003
    git am --signoff < /vagrant/src/libs/patches/rename_libev_symbol.patch
    ./configure
    make
    sudo make install

    cd /vagrant/src/libs
    git clone https://github.com/redis/hiredis/
    cd /vagrant/src/libs/hiredis
    git checkout 3d8709d19d7fa67d203a33c969e69f0f1a4eab02
    git am --signoff < /vagrant/src/libs/patches/rename_hiredis_symbol.patch
    make
    sudo make install

    cd /vagrant/src/libs
    git clone https://github.com/dcrankshaw/redox
    cd /vagrant/src/libs/redox
    git checkout 2272f30f18890a6c18632ac3a862b0a879489b40
    git am --signoff < /vagrant/src/libs/patches/rename_redox_symbol.patch

    cd /vagrant/src/libs
    git clone https://github.com/facebook/folly.git
    cd folly/folly

    autoreconf -ivf
    ./configure
    make
    sudo make install

    cd /vagrant
    ./configure
    cd debug/
    make

    ../bin/run_unittests.sh

  SHELL
end
