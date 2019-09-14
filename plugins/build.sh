#!/bin/bash

trap "rm -f plugin_$$.c" 0 1 2 15

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

if [ "$out" = "" ]; then
	echo "Error: no PLUGIN statement found"
	exit 1;
fi

cat "$script" | awk -v file="$script" '
	BEGIN {
#		print "#include \"plugin.inc\""
	} 
	/^%END_PLUGIN/ {copy=0} 
	copy==1 
	$1 ~ /^%BEGIN_PLUGIN/ { 
		printf "#line %d \"%s\"\n", NR+1, file
		copy=1
	}  
	' >plugin_$$.c

[ `uname -s` == "Linux" ] && LDFLAGS="$LDFLAGS -shared -Wall -fPIC -Wl,-soname,$out,-undefined,dynamic_lookup"
[ `uname -s` == "Darwin" ] && LDFLAGS="$LDFLAGS -dynamiclib -Wall -pedantic -fPIC -Wl,-undefined,dynamic_lookup"

CWLIB=../iod/lib/clockwork_interpreter/src/lib_clockwork_interpreter/
echo gcc $CFLAGS $LDFLAGS -I /usr/local/include -I$CWLIB plugin_$$.c -o "$out" 
gcc $CFLAGS $LDFLAGS -I$CWLIB plugin_$$.c -o "$out" && rm plugin_$$.c
