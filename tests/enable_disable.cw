/* A test of enabling and disabling a list of machines

  The enable/disable process should be done in order of dependency
*/

b1 Basic;
b2 Basic;
b3 Basic;

a1 Controller b2,b3;
a2 Controller b3,b1;

all LIST a1,a2,b1,b2,b3;

m1 Master all; 
startup STARTUP m1;


Basic MACHINE { 
	Idle DEFAULT;
}

Controller MACHINE a,b {
	Idle DEFAULT;
}

Master MACHINE machines {
	Idle DEFAULT;
}

STARTUP MACHINE machines {
	COMMAND start { ENABLE machines; }
	COMMAND stop { DISABLE machines; }
}
