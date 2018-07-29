


DualPortMotor MACHINE power_input_A, power_input_B {
    OPTION VALUE 0; # motor drive power -32767 .. 0 .. 32767. Stationary is 0
    LOCAL OPTION Kspeed 333; # a scalar to convert power to speed in mm/sec
    LOCAL OPTION scale 1000;
    LOCAL OPTION zero 3000; # the amount to move from centre before motion occurs
    LOCAL OPTION max_value 32767;
    LOCAL OPTION hidden "scale";
    
    # at power of 15000 (12000 after taking the zero point into account) 
    # the conveyor moves at 3000mm/sec
    # therefore Kspeed = 1 / 12 * 4  = 0.333. 
    # since we have no floating point, use 333 / 1000

	forward_braking WHEN VALUE>=zero AND power_input_B.VALUE > 0;
	backward_braking WHEN VALUE<=-zero AND power_input_A.VALUE > 0;
    forward WHEN VALUE >= zero;
    backward WHEN VALUE <= -zero;
    stopped DEFAULT;
    
    #TBD. this RECEIVE works for analogue inputs but not for property changes.
    RECEIVE power_input_A.changed_enter {
        VALUE := power_input_A.VALUE - power_input_B.VALUE;
    }

    RECEIVE power_input_B.changed_enter {
        VALUE := power_input_A.VALUE - power_input_B.VALUE;
    }
	ENTER forward { direction := 1 }
	ENTER backward { direction := -1 }
	ENTER stopped { direction := 0 }
    
    
    COMMAND stop { power_input_a.VALUE := 0; power_input_B.VALUE := 0 }
    COMMAND forward WITHIN stopped { power_input_A.VALUE := zero }
    COMMAND backward WITHIN stopped { power_input_B.VALUE := zero }
    COMMAND faster WITHIN forward { 
		IF (power_input_A.VALUE+100 < power_input_A.max) {
			power_input_A.VALUE := power_input_A.VALUE + 100 
		}
	}
    COMMAND faster WITHIN forward_braking { 
		IF (power_input_B.VALUE >=100) {
			power_input_B.VALUE := power_input_B.VALUE - 100 
		}
		ELSE {
			power_input_B.VALUE := 0;
		}
	}
    COMMAND faster WITHIN backward{ 
		IF (power_input_B.VALUE + 100 <= power_input_B.max) {
			power_input_B.VALUE := power_input_B.VALUE + 100 
		}
	}
    COMMAND faster WITHIN backward_braking { 
		IF (power_input_A.VALUE >=100) {
			DEC power_input_A.VALUE BY 100;
		}
		ELSE {
			power_input_B.VALUE := 0;
		}
	}
    COMMAND slower WITHIN forward { 
		IF (power_input_A.VALUE >= zero + 100) {
			DEC power_input_A.VALUE BY 100;
		}
		ELSE {
			power_input_A := 0;
		}
	}
    COMMAND slower WITHIN forward_braking { 
		IF (power_input_B.VALUE + 100 <= power_input_B.max) {
			INC power_input_B.VALUE BY 100;
		}
	}
    COMMAND slower WITHIN backward { 
		IF (power_input_B.VALUE > zero + 100) {
			DEC power_input_B.VALUE BY 100;
		}
		ELSE {
			power_input_B.VALUE := 0;
		}
	}
    COMMAND slower WITHIN backward_braking { 
		IF (power_input_A.VALUE + 100 <= power_input_A.max) {
			INC power_input_A.VALUE BY 100;
		}
	}
}

PowerInput MACHINE {
	OPTION VALUE 0;
	OPTION max 30000;
	LOCAL OPTION last_value 0;
	changed WHEN VALUE != last_value;
	idle DEFAULT;

	ENTER changed { last_value := VALUE}
}

PortA PowerInput;
PortB PowerInput;
Motor DualPortMotor PortA, PortB;


