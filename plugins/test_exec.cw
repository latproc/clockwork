
Test MACHINE {
	exec SystemExec;
	
	update WHEN SELF IS idle AND TIMER > 1000;
	idle DEFAULT;

	COMMAND test {
		exec.Command := "sleep 5";
		SEND start TO exec;
	}
}

test Test;
