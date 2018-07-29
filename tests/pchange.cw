# example of the use of the PROPERTY_CHANGE message
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
