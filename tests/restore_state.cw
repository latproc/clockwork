# test of setting state based on a property

TestStateRestorer MACHINE other {
	fixing WHEN OWNER != OWNER.saved_state; ENTER fixing { SET OWNER TO saved_state; }
	idle DEFAULT;
}

Test MACHINE {
	OPTION saved_state one;
	
	fixer TestStateRestorer SELF;
	one STATE;
	two STATE;
	three STATE;
}

test Test;

