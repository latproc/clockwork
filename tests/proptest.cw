Test MACHINE device {

	OPTION requested "on";

	ENTER INIT { 
		SET device TO requested; 
	}
}

dev FLAG;
control Test dev;
