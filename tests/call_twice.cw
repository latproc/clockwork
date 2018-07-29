# test the behaviour of calling a method that is already in progress 
# for someone else

Delay MACHINE {
	OPTION total 0;
	COMMAND work { start := NOW; WAIT 2000; total :=  total + NOW - start; }
}

Worker MACHINE {
	delay Delay;
	OPTION worked 0;
	COMMAND work { worked := worked + 1;  CALL work ON delay; }
	idle DEFAULT;
}

worker Worker;

Master MACHINE worker {
	OPTION delay 50;

	idle DEFAULT;

	COMMAND check { LOG "got command";  WAIT delay; LOG "checking"; CALL work ON worker; LOG "checked"; }
}

master1 Master(delay:50) worker;
master2 Master(delay:500)worker;

Clock MACHINE {
	on WHEN SELF IS off; ENTER on { WAIT 999; }
	off DEFAULT; 		ENTER off { WAIT 999; }
}
clock Clock;
