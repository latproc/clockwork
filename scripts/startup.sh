#!/bin/bash
# Example host bootstrap — site-specific (NIC names, storage, launch).
# Do not use scripts/ethercat.sh on the elc branch; load elc_ethercat per TRANSPORT.md.

echo deadline > /sys/block/sda/queue/scheduler
echo 1 > /sys/block/sda/queue/iosched/fifo_batch
# IgH helper removed on this branch:
# /opt/latproc/scripts/ethercat.sh start enp2s0

sysctl -w net.core.default_qdisc=pfifo_fast

/opt/latproc/scripts/launch
