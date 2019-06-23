# order of evaluation test

Test MACHINE {
	one INITIAL;
    two STATE;

	ENTER one { SET SELF TO two; LOG "one"; }
	ENTER two { LOG "two"; }
}
test Test;

StateMemoryTest MACHINE {
	a INITIAL;
	b STATE;
	c STATE;

	COMMAND save { saved := SELF }
	COMMAND restore { SET SELF TO saved; }
}
state_memory_test StateMemoryTest;

PrivateStateTest MACHINE {
	a INITIAL;
	x PRIVATE STATE;
	b STATE;
}
PrivateStateTestMonitor MACHINE pst {
  idle INITIAL;
}
private_test PrivateStateTest;
pstmon PrivateStateTestMonitor private_test;

