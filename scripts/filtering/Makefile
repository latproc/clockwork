all : bwlp bwhp bwbp bwbs

bwlp : bwlp.c iir.h libiir.a
	gcc -o bwlp bwlp.c -L. -lm -liir

bwhp : bwhp.c iir.h libiir.a
	gcc -o bwhp bwhp.c -L. -lm -liir

bwbp : bwbp.c iir.h libiir.a
	gcc -o bwbp bwbp.c -L. -lm -liir

bwbs : bwbs.c iir.h libiir.a
	gcc -o bwbs bwbs.c -L. -lm -liir

libiir.a : liir.o
	ar cr libiir.a liir.o

liir.o : liir.c iir.h
	gcc -c liir.c

clean:
	rm -f bwbp bwbs bwhp bwlp libiir.a liir.o rdf 
