#!/bin/sh

file="$1"
[ $1 = "" ] && file="modbus_mappings.txt"

[ "$name" = "" ] && name="SIXHEAD"

cat "$file" \
	| tr : ' ' \
	| awk -v name="$name" '{n=1; if ($4=="Ascii_string") n=128; 
			printf "211,%s,%s,%s,%d,FALSE,%c%s%s,0,0\n",name,$3,$4,n,39,$1,substr($2,2,4)}'

