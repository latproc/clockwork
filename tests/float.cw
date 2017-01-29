#floating point value tests

Sample MACHINE {
	OPTION i 7;
	OPTION x 0.1;
	OPTION y 1;
	OPTION z 1.1E3;
	OPTION z1 1.E4;
	OPTION K1 1000.34;

	COMMAND float { 
		y := ( (i+1) AS FLOAT );
	}
	COMMAND calc {
		y := x + 1;
		i := (x + 2) AS INTEGER;
		y := (i AS FLOAT + 2) / 2;
		z := i AS FLOAT / 2;
	}

	COMMAND show {
		LOG "x: " + x;
		LOG "y: " + y;
		LOG "z: " + z;
		LOG "x + y: " + (x + y);
		LOG "x + z: " + (x + z);
		LOG "x / y: " + (x / y);
		LOG "x / z: " + (x / z);

		LOG "1+1=" + (1 + 1);
		LOG "x as int: " + (x AS INTEGER);
		LOG "i as float: " + (i AS FLOAT);
	}

	# multiplication tests
	COMMAND mul {
		# mul/div zero
		x := 0.0;
		y := K1 * x;
		z := x / 4;
		LOG "" + K1 + " * 0.0" + ", 0.0 / 4 " + z;
	}

	# check whether a cast works when applied to the result value
	COMMAND cast {
		x := 2.3;
		x := x AS INTEGER;
		LOG "x as int: " + x;
		x := x AS FLOAT;
		LOG "x as float: " + x;
	}

	COMMAND compare {
		IF (1.1 > 1.05) { LOG "1.1>1.05"; };
		IF (0.1 >= 1 - 0.9) { LOG "0.1 >= 1 - 0.9" };
		IF (1.05 < 1.1) { LOG "1.05<1.1"; };
		IF (1 - 0.9 <= 0.1) { LOG "1 - 0.9 <= 0.1" };
	}
}
sample Sample;

Line MACHINE {
	OPTION MAX 10000;
	OPTION MIN -10000;
	OPTION x1 0.0, x2 0.0, y1 0.0, y2 0.0, m 0.0;
	RECEIVE PROPERTY_CHANGE {
		IF (x2-x1 == 0.0) { m := MAX; RETURN; };
		m := (x2 - x1) AS FLOAT / (y2 - y1);
	}
}
line Line;

Cast MACHINE {
	OPTION x 1.1;
	OPTION i 0;
	COMMAND test {
		i := x AS INTEGER;
	}
}

cast Cast;
