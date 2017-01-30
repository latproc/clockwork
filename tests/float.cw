#floating point value tests

Sample MACHINE {
	OPTION i 7;
	OPTION x 0.1;
	OPTION y 1.0;
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

		y := 100.0;
		x := y AS FLOAT;
		LOG "y: " + y + " y as float: " + x;
	}

	COMMAND compare {
		IF (1.1 > 1.05) { LOG "1.1>1.05"; };
		IF (0.1 >= 1 - 0.9) { LOG "0.1 >= 1 - 0.9" };
		IF (1.05 < 1.1) { LOG "1.05<1.1"; };
		IF (1 - 0.9 <= 0.1) { LOG "1 - 0.9 <= 0.1" };
	}
}
sample Sample;

Settings MACHINE {
	OPTION k1 10000.0, k2 100.0;
}
	
Cast MACHINE settings{
	OPTION x 1.1;
	OPTION y 0, z 0;
	OPTION res 0;
	OPTION i 0;
	COMMAND test {
		IF (1 == 1) {
			i := x AS INTEGER;
			y := settings.k1 AS FLOAT;
			z := y * 105 AS FLOAT;
			x := z;
		}
	}
}
settings Settings;
cast Cast settings;
