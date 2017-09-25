# test the behaviour of calling a method that is already in progress 
# for someone else

Worker MACHINE {
	working DURING work { WAIT 2000; }
	idle DEFAULT;
}

worker Worker;

Master MACHINE worker {
	idle DEFAULT;

	COMMAND check { CALL work ON worker; }
}

master1 Master worker;
master2 Master worker;
