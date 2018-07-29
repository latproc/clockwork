# Refuse to transition from one state to another

#currently this test fails by leaving the machine in the target state.

TestAbortInTransition MACHINE {

	a STATE;
	b STATE;

	COMMAND abort { THROW bad_transition; }
	TRANSITION a TO b USING abort;

	CATCH bad_transition { DISABLE SELF; RESUME SELF; }
}
test TestAbortInTransition;
