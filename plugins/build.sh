#!/bin/bash

#trap "rm -f plugin_$$.c" 0 1 2 15

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

if [ $(uname -s) == "Darwin" ]; then
    LDFLAGS="$LDFLAGS -dynamiclib -Wall -pedantic -fPIC -Wl,-undefined,dynamic_lookup"
    echo gcc $CFLAGS $LDFLAGS -I../iod/src plugin_$$.c -o "$out"
    extra_libs=""
    grep -q 'exec_web_request' plugin_$$.c && extra_libs="$extra_libs -lcurl"
    grep -q 'exec_digest' plugin_$$.c && extra_libs="$extra_libs -lcrypto"
    gcc $CFLAGS $LDFLAGS -I../iod/src plugin_$$.c $extra_libs -o "$out" && rm plugin_$$.c
elif [ $(uname -s) == "Linux" ]; then
    LDFLAGS="$LDFLAGS -shared -Wall -fPIC -Wl,-soname,$out"
    echo gcc $CFLAGS -rdynamic $LDFLAGS -I../iod/src plugin_$$.c -o "$out"
    extra_libs=""
    grep -q 'exec_web_request' plugin_$$.c && extra_libs="$extra_libs -lcurl"
    grep -q 'exec_digest' plugin_$$.c && extra_libs="$extra_libs -lcrypto"
    gcc $CFLAGS $LDFLAGS -I../iod/src plugin_$$.c $extra_libs -o "$out" && rm plugin_$$.c
else
    echo "unknown platform $(uname -s)"
fi
