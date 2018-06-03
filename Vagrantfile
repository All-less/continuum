# -*- mode: ruby -*-
# vi: set ft=ruby :

Vagrant.configure("2") do |config|

  config.vm.provider "virtualbox" do |vb|
    vb.memory = "4096"
  end

  # As we need to patch redox, we exclude it from sync.
  config.vm.synced_folder ".", "/vagrant", type: "rsync",
    rsync__exclude: [
        ".git/", "debug/",
        "src/libs/redox",
        "src/libs/folly",
        "src/libs/libev",
        "src/libs/hiredis"
    ]

  config.vm.box = "debian/testing64"

  # Enable provisioning with a shell script. Additional provisioners such as
  # Puppet, Chef, Ansible, Salt, and Docker are also available. Please see the
  # documentation for more information about their specific syntax and use.
  config.vm.provision "shell", privileged: false, inline: <<-SHELL
    ../bin/init_dev_environment.sh
  SHELL
end
