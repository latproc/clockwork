# test the SQRT operation

Test MACHINE {
	OPTION x 9;
	OPTION y 0;

	ok WHEN SQRT x == 3.0;
	idle DEFAULT;

	ENTER ok {
		y := SQRT (x + 7);
		LOG "sqrt result: " + y;
		SHUTDOWN;
	}
}

test Test;
