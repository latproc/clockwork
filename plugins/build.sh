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
	' >plugin_$$.c

[ `uname -s` == "Linux" ] && LDFLAGS="-shared -Wl,-soname,$out"
[ `uname -s` == "Darwin" ] && LDFLAGS="-dynamiclib -Wl,-undefined,dynamic_lookup"

gcc $LDFLAGS -I /usr/local/include -I../iod plugin_$$.c -o "$out" 
rm plugin_$$.c
