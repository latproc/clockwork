#!/bin/sh
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
	rmmod ec_master
	modprobe e1000e InterruptThrottleRate=0
elif [ "$1" = "start" ]; then
	modprobe ec_master main_devices=$MAC
	modprobe ec_generic
	ifconfig $IF up
	ethtool -C $IF rx-usecs 0 rx-frames 1 tx-usecs 0 tx-frames 1
	[ -e /dev/EtherCAT0 ] && chmod go+rw /dev/EtherCAT0
fi
