# test the ABS operation

Test MACHINE { 
	OPTION x -1;
	zero WHEN ABS x < 0.001;
	idle DEFAULT;

	COMMAND run {
		y := ABS x;
		LOG "x: " + x + " y:" + y;
	}
}
test Test;
