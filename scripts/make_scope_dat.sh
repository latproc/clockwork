#!/bin/sh

cat $1 | awk '{print $2}' | sort -u | awk '{print $0,NR}' > scope.dat
