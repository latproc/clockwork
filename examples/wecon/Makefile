
PROG=wecon

all:	$PROG TEST;

$PROG:	wecon.c
	gcc -o wecon wecon.c -lethercat -lrt

TEST: wecon.c
	gcc -o wecon-test -DTESTING wecon.c -lethercat -lrt
