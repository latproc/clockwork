# test of an invalid call to a machine in the wrong state

Target MACHINE {
	safe STATE;
	unsafe INITIAL;
	COMMAND run WITHIN safe	{ LOG SELF.NAME + " got run command"; }
}
target Target;

Master MACHINE other {
	running WHEN SELF IS running;
	idle DEFAULT;

	running DURING run  { CALL run ON other; SET SELF TO idle; }
}
master Master target;
	

