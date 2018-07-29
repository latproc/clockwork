
a FLAG;
b FLAG;
c FLAG;
d FLAG;
e FLAG;

l LIST a,b,c,d,e;

Test MACHINE list {
	l1 LIST;

	ENTER INIT { MOVE 3 FROM list TO l1; MOVE ALL FROM l1 TO list; }
	COMMAND back { MOVE LAST FROM list TO l1; INSERT l1
}
test Test l;
