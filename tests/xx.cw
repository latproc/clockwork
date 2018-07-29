X MACHINE {
	b FLAG;
}
a X;

FOLLOW MACHINE x {
	on WHEN x IS on;
	off WHEN x IS off;
	unknown DEFAULT;
}
#f FOLLOW a.b;


TEST MACHINE {
f REFERENCE a.b;

#ENTER INIT { ASSIGN a.b TO f; }
}
test TEST;
