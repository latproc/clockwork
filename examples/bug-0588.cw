PFLAG MACHINE {
	OPTION PERSISTENT true;
	OPTION export rw;

	on WHEN VALUE IS on;
	off DEFAULT;

	COMMAND turnOn {
		VALUE := on;
	}

	COMMAND turnOff {
		VALUE := off;
	}

	ENTER INIT {
		VALUE := off;
	}

	TRANSITION off TO on ON turnOn;
	TRANSITION on TO off ON turnOff;
}

VIRTUALLASTMOVED MACHINE INPUT, O_A, O_B {
    OPTION NoMoveTimer 110;

    changing WHEN SELF != LastB && O_B IS on || SELF != LastA && O_A IS on,
        EXECUTE HomedA WHEN TIMER>NoMoveTimer && O_A IS on,
        EXECUTE HomedB WHEN TIMER>NoMoveTimer && O_B IS on;

    LastA WHEN SELF IS LastA || SELF != LastA && O_A IS on && INPUT IS off;
    LastB WHEN SELF IS LastB || SELF != LastB && O_B IS on && INPUT IS off;

    Unknown DEFAULT;

    RECEIVE HomedA { SET SELF TO LastA; LOG "Homed A" }
    RECEIVE HomedB { SET SELF TO LastB; LOG "Homed B" }
    
    ENTER INIT { LOG NoMoveTimer }
}

in PFLAG (tab:tests);
a FLAG (tab:tests);
b FLAG (tab:tests);
#test VIRTUALLASTMOVED (tab:tests) in, a, b;

# Take a single input which is used to indicate both 
# extended and retracted and create two inputs out of this information
VIRTUALACTUATOR MACHINE Input, O_A, O_B {
        OPTION PERSISTENT true;
        OPTION NoMoveTimer 105;
        Last VIRTUALLASTMOVED (tab:BaleHandling) Input, O_A, O_B;

        A_Stable WHEN SELF IS A_Moving && Input IS on || Last IS LastA && Input IS on;
        B_Stable WHEN SELF IS B_Moving && Input IS on || Last IS LastB && Input IS on;
        A_Moving WHEN Last IS LastA && O_A IS on;
        B_Moving WHEN Last IS LastB && O_B IS on;
        off WHEN Input IS off && O_A IS off && O_B IS off;

        Unknown DEFAULT;

        ENTER Unknown {
            LOG "VIRTUALLASTMOVED timer updated to " + NoMoveTimer;
                Last.NoMoveTimer := NoMoveTimer;
        }
        LEAVE INIT { Last.NoMoveTimer := NoMoveTimer; } 
}

VIN_a MACHINE actuator {
    OPTION type "Input";
    on WHEN actuator IS A_Stable;
    off DEFAULT;
}

VIN_b MACHINE actuator {
    OPTION type "Input";
    on WHEN actuator IS B_Stable;
    off DEFAULT;
}

Controller MACHINE actuator {
    ENTER INIT {
        actuator.NoMoveTimer := 3000;
        actuator.Last.NoMoveTimer := 5000;
        LOG "init done";
        LOG actuator.Last.NoMoveTimer;
    }
}

actuator VIRTUALACTUATOR in, a, b;
va VIN_a actuator;
vb VIN_b actuator;
controller Controller actuator;

