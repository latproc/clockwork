
Test MACHINE {
  OPTION duration 5;
	exec SystemExec;

	update WHEN SELF IS idle AND TIMER > 1000;
	idle DEFAULT;

	COMMAND test {
		exec.Command := "/bin/sleep " + duration;
		SEND start TO exec;
	}
}

test Test;
