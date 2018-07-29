# This file generates errors to test the error logging functionality

Piston MACHINE extend_sol, retract_sol, extended_proc, retracted_prox {
	OPTION a 0;
	poll WHEN TIMER >= 1000;
	test DEFAULT;

	ENTER test{ a:= a + 1; LOG "This is test number " + a }
}

es FLAG;
rs FLAG;
ep FLAG;
rp FLAG;
piston Piston es, rs, ep, rp;

