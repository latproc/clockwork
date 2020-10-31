#!/bin/bash

cd /usr/src
git clone https://github.com/icshwi/etherlabmaster.git
cd etherlabmaster/

# Set the correct Ethernat Interface
echo "ETHERCAT_MASTER0=enp2s0" > ethercatmaster.local

echo "WITH_PATCHSET = YES" > configure/CONFIG_OPTIONS.local
echo "ENABLE_HRTIMER = YES" >> configure/CONFIG_OPTIONS.local
echo "ENABLE_EOE = YES" >> configure/CONFIG_OPTIONS.local
make patchset
make showopts 
-->IS Everything enabled ?
make build
make install
--> Enable DKMS
make dkms_add
make dkms_build
make dkms_install
make setup

if [ -d /opt/latproc -L /opt/latproc ]; then
cd  /opt/latproc/
./scripts/prepare_ec_tool /usr/src/etherlabmaster-1.5.2-patchset
else
echo etherlab installed. to integrate with latproc, run:
echo
echo scripts/prepare_ec_tool /usr/src/etherlabmaster-1.5.2-patchset
echo
echo from the root of the latproc directory
fi
