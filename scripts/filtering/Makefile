all : bwlp bwhp bwbp bwbs

bwlp : bwlp.c iir.h libiir.a
	gcc -o bwlp bwlp.c -L. -liir -lm

bwhp : bwhp.c iir.h libiir.a
	gcc -o bwhp bwhp.c -L. -liir -lm

bwbp : bwbp.c iir.h libiir.a
	gcc -o bwbp bwbp.c -L. -liir -lm

bwbs : bwbs.c iir.h libiir.a
	gcc -o bwbs bwbs.c -L. -liir -lm

libiir.a : liir.o
	ar cr libiir.a liir.o

liir.o : liir.c iir.h
	gcc -c liir.c

clean:
	rm -f bwbp bwbs bwhp bwlp libiir.a liir.o rdf 
