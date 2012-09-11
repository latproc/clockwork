#!/bin/sh
#
# ethercat control

if [ "$2" = "" ]; then
IF=eth1
else
IF=$2
fi
MAC=`ifconfig $IF |grep -E -o '([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}'`


echo "using interface $IF, hwaddr $MAC"

if [ "$1" = "stop" ]; then
ifconfig $IF down
rmmod ec_generic
#rmmod ec_e1000
#rmmod e1000e
rmmod ec_master
modprobe e1000e InterruptThrottleRate=0
elif [ "$1" = "start" ]; then

#rmmod e1000e
#modprobe ec_e1000 InterruptThrottleRate=0
#modprobe ec_master main_devices=00:04:23:ab:d7:90
#modprobe ec_master main_devices=00:04:23:ab:d7:df
modprobe ec_master main_devices=$MAC
#modprobe e1000e InterruptThrottleRate=0
modprobe ec_generic
ifconfig $IF up
 ethtool -C eth1 rx-usecs 0 rx-frames 1 tx-usecs 0 tx-frames 1
[ -e /dev/EtherCAT0 ] && chmod go+rw /dev/EtherCAT0
fi
