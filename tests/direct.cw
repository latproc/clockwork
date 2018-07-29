Driver MACHINE { OPTION direction 0; Idle INITIAL; }

Controller MACHINE driver, brake {
	OPTION D 0;
	ENTER INIT { LOG "f: " + f.direction  + " g: " + g.direction }

	f Driver (direction:D);
	g Driver (direction:D);
}

a Controller(D:1) f, g;
b Controller(D:-1) g, f;

