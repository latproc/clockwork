#!/bin/sh

usage() {
	echo "usage: $0 script"
	exit 2
}
script="$1"
[ "$1" = "" ] && usage;

out=`cat $script | awk '
	$0 ~ /.*PLUGIN[^"]*"[a-zA-Z0-9_.]+".*;/ {
	sub(/.*PLUGIN[^"]*"/,"",$0)
	sub(/".*/,"",$0)
	print 
	}'`

cat "$script" | awk '
	BEGIN {print "#include \"plugin.inc\""} 
	/^%END_PLUGIN/ {copy=0} 
	copy==1 
	$1 ~ /^%BEGIN_PLUGIN/ { copy=1; }  
	' >plugin_$$.cpp

[ `uname -s` == "Linux" ] && LDFLAGS=-shared -Wl,-soname,$out
[ `uname -s` == "Darwin" ] && LDFLAGS=-dynamiclib

g++ $LDFLAGS -I /usr/local/include -I../iod plugin_$$.cpp globals.cpp -L/usr/local/lib -lboost_system -o "$out" lib/*.o -lzmq -lmosquitto -lboost_filesystem
rm plugin_$$.cpp
