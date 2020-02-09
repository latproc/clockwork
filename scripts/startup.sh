#!/bin/bash

echo deadline > /sys/block/sda/queue/scheduler
echo 1 > /sys/block/sda/queue/iosched/fifo_batch
/opt/latproc/scripts/ethercat.sh start enp2s0

sysctl -w net.core.default_qdisc=pfifo_fast
