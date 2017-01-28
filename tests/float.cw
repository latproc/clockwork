#floating point value tests

Sample MACHINE {
	OPTION i 7;
	OPTION x 1.1;
	OPTION y 1;
	OPTION z 1.1E3;
	OPTION z1 1.E4;

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
}
sample Sample;

Cast MACHINE {
	OPTION x 1.1;
	OPTION i 0;
	COMMAND test {
		i := x AS INTEGER;
	}
}
cast Cast;
