PIDDIRECTIONCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT SlowSpeed, FullSpeed, StoppingTime, StoppingDistance, Tolerance, Kp, Ki, Kd;
	EXPORT RO 16BIT PowerOffset;
	OPTION SlowSpeed 40;
	OPTION FullSpeed 1000;

# settings that are specific to forward/reverse directions and are not provided in PIDCONFIGURATION
	OPTION Tolerance 100;          # stopping position tolerance
	OPTION RampTime 500;
	OPTION StartupPowerOffsets "0"; # a list of recent power offsets, averaged to provide PowerOffset
	OPTION PowerOffset 0;	# the current power offset used when starting movement in this direction
	OPTION StoppingDistance 3000;  # distance from the stopping point to begin slowing down

# settings that override values given in PIDCONFIGURATION 
	OPTION StoppingTime 300;  # 300ms stopping time
	OPTION Kp 0;
	OPTION Ki 0;
	OPTION Kd 0;
}


PIDCONFIGURATION MACHINE {
	OPTION PERSISTENT true;
	EXPORT RW 32BIT MinUpdateTime, StartupTime, StoppingTime, Kp, Ki, Kd;

	OPTION MinUpdateTime 20; # minimum time between normal control updates
	OPTION StartupTime 500;   # 500ms startup ramp time used for both forward and reverse cannot be overriden in PIDSPEEDCONFIGURATION
	OPTION StoppingTime 300;  # 300ms stopping time used if the forward/reverse values are not given
	OPTION Inverted false;     # do not invert power
	OPTION RampLimit 1000;		# maximum change in control power per cycle

	OPTION Kp 1000000;
	OPTION Ki 0;
	OPTION Kd 0;
}

PIDSTATISTICS MACHINE {
	OPTION Vel 0;
	OPTION SetPt 0;
	OPTION Pwr 0;
	OPTION Err_p 0;
	OPTION Err_i 0;
	OPTION Err_d 0;

	Idle INITIAL;
	Busy STATE;
	ENTER Busy { Err_p := 0; Err_i := 0; Err_d := 0; }
}

PIDCONTROLLER MACHINE M_Control, settings, output_settings, fwd_settings, rev_settings, driver, pos {

	EXPORT RW 32BIT Velocity, StopError;
	OPTION SetPoint 0;
	OPTION StopPosition 0;
	OPTION StopMarker 0;
	OPTION DEBUG 0;
	OPTION Velocity 0;            # estimated current velocity 
	OPTION StopError 0;
	OPTION Position 0;

  stats PIDSTATISTICS;

	# This module uses the restore state to give the machine 
	# somewhere to go once the driver is no longer interlocked. 
	# Since we are not using stable states for stopped, seeking etc we 
	# the machine would otherwise never leave interlocked.

	interlocked WHEN driver IS interlocked OR abort IS on;
	seeking WHEN SELF IS interlocked AND LastState == "seeking";
	speed WHEN SELF IS interlocked AND LastState == "speed";
	restore WHEN SELF IS interlocked; # restore state to what was happening before the interlock
	stopped INITIAL;
	seeking STATE;
	speed STATE;
	atposition STATE;
	
	OPTION LastState "unknown";
	
	abort FLAG;

	ENTER interlocked {
		SET M_Control TO Unavailable;
	}
	ENTER stopped { 
		SET M_Control TO Ready;
		StopPosition := 0; 
		StopMarker := 0; 
	}
	ENTER seeking {
    	LastState := "seeking";
		SET M_Control TO Resetting;
	}
	ENTER speed { 
    	LastState := "speed";
		SET M_Control TO Working;
	}
	ENTER atposition { 
    	LastState := atposition;
		SET M_Control TO Ready;
	}
	ENTER restore { SET SELF TO stopped; } 

	COMMAND stop { SET SELF TO stopped; }

	COMMAND MarkPos {
		StopMarker := pos.VALUE;
		IF (SetPoint == 0) {
			StopPosition := StopMarker;
		};
		IF (SetPoint > 0) {
			StopPosition := StopMarker + fwd_settings.StoppingDistance;
		};
		IF (SetPoint < 0) {
			StopPosition := StopMarker - rev_settings.StoppingDistance;
		};
		SET SELF TO seeking;
	}

	# convenience commands
	COMMAND slow {
	   SetPoint := fwd_settings.SlowSpeed; 
		SET SELF TO speed;
	}
	
	COMMAND start { 
    SetPoint := fwd_settings.FullSpeed;
		SET SELF TO speed;
  }
	
	COMMAND slowrev { 
	  SetPoint := rev_settings.SlowSpeed;
		SET SELF TO speed;
	}

	COMMAND startrev { 
	  SetPoint := rev_settings.FullSpeed;
		SET SELF TO speed;
	}

	COMMAND clear {
		StopPosition := pos.VALUE;
		SET SELF TO seeking;
	}
}
