# run this using:
#
# cw -i persist.dat stdchannels.cw pidtest.cw pid2_controller.cw
#
# and run persistd from the same directory as the persist.dat
#

conveyor PIDCONTROLLER states, settings, output_settings, fwd_settings, rev_settings, driver, conveyor_pos;

states STANDARDCONTROL;
settings PIDCONFIGURATION;
output_settings GLOBALDRIVER ;
fwd_settings PIDDIRECTIONCONFIGURATION(StoppingDistance:8000, Kp:0, Ki:100);
rev_settings PIDDIRECTIONCONFIGURATION(StoppingDistance:8000);
conveyor_guard Guard;
output_enable InitiallyOn;
driver ANALOGDUALGUARD conveyor_guard, output_enable, motor_power;

# simulated hardware IO
motor_power SIMANALOGUEOUTPUT (tab:motor);  # analog output to the motor this changes state to generate messages for the SIMOTOR
conveyor_pos SIMANALOGUEINPUT(tab:motor);        # quadrature input for the encoder

# simulation drivers to provide reasonable data at the simulated hardware
conveyor_encoder SIMENCODER(tab:motor) conveyor_pos;   # generates simulated position for the quadrature encoder
sim_motor SIMOTOR(tab:motor, Kspeed:1500) motor_power;
sim_conveyor SIMCONVEYOR(tab:motor) sim_motor;         # tracks whether the conveyor is moving and if it is fwd/rev
sim_monitor SIMCONVEYORMONITOR(tab:motor)              # computes position for the encoder based on motor speed
    sim_conveyor, sim_motor, conveyor_encoder;  

Guard MACHINE {
    true INITIAL;
    false STATE;
}

InitiallyOn MACHINE {
    on INITIAL;
    off STATE;
}

STANDARDCONTROL MACHINE {
  Unavailable INITIAL;
  Ready STATE;
  Working STATE;
  Done STATE;
  Resetting STATE;
  Error STATE;
}

# this output driver takes an analogue output or guarded analogue
# output such as an ANALOGDUALGUARD and drives the output in the
# range 0..32767. 
#
# The zero position is the midpoint of the range: 16383
# An error is given if the input value 'NewValue' is out of range.

GLOBALDRIVER MACHINE {
	OPTION PERSISTANT true;
	EXPORT RW 32BIT MaxForward, MaxReverse, ZeroPos, MinForward, MinReverse;
	OPTION MaxForward 32767;
	OPTION MaxReverse 0;
	OPTION ZeroPos 16383;
	OPTION MinForward 18000;
	OPTION MinReverse 14000;
}

# A_Output Must be set to ZeroPos before turning on O_Enable, which must be supplied as a config property
ANALOGDUALGUARD MACHINE Guard, O_Enable, A_Output {
  OPTION PERSISTENT true;
  OPTION VALUE 0;
  OPTION ZeroPos 16383;
  Updating FLAG;

  disabled WHEN A_Output DISABLED;
  interlocked_check WHEN SELF IS interlocked && A_Output.VALUE != ZeroPos;
  interlocked WHEN Guard != true;
  on WHEN O_Enable IS on && VALUE != ZeroPos,
    TAG Updating WHEN VALUE != A_Output.VALUE;
  off DEFAULT;

  RECEIVE Updating.on_enter { A_Output.VALUE := VALUE; }

  ENTER disabled {
    A_Output.VALUE := ZeroPos + 1;
  }
  ENTER interlocked_check {
    VALUE := ZeroPos;
    A_Output.VALUE := VALUE;
  }

  ENTER interlocked {
    VALUE := ZeroPos;
    A_Output.VALUE := VALUE;
  }

  ENTER off {
    VALUE := ZeroPos;
    A_Output.VALUE := ZeroPos;
  }

  ENTER INIT {
    VALUE := ZeroPos;
    A_Output.VALUE := VALUE;
    WAIT 2;
  }
}


SIMENCODER MACHINE anain {
    OPTION new_value 0, position 0, last 0, start_pos 1000000;
    OPTION max 2000000000;
    OPTION PERSISTENT true;
    OPTION hidden "max,new_value,last";

    updating WHEN last != new_value; 
    ENTER updating {
        IF (new_value > max) { new_value := -max; };
        IF (new_value < -max) { new_value := max; };
        position := new_value;
        anain.VALUE := position; last := position; 
        SET SELF TO idle;
    }
    ENTER INIT { new_value := start_pos; }
    idle DEFAULT;
}

# conveyors have a length in counter pulses as well as a physical dimension
# the scaling factor between these two amount is automatically calculated
# for use in visualisation programs

SIMCONVEYOR MACHINE motor {
    OPTION length 3000; # counter pulses
    OPTION length_mm 1500; # millimetres
    OPTION scale 0;         # converts length in pulses to length in mm
    OPTION start 0;
    OPTION hidden "start";
    
    is_forward FLAG(tab:SimulateBales);
    is_reverse FLAG(tab:SimulateBales);
    
    moving WHEN motor IS turning;
    stationary DEFAULT;
    ENTER INIT { scale := length_mm * 1000 / length; }
}

SIMANALOGUEOUTPUT MACHINE {
    OPTION VALUE 16383;
    OPTION last 0;
    OPTION type AnalogueOutput;
    OPTION hidden "last";
    changed WHEN SELF IS changing;  ENTER changed { } # generate an event when the value changes
    changing WHEN last != VALUE;    ENTER changing { last := VALUE;  } # generate an event when the value changes
    idle DEFAULT;
}

SIMANALOGUEINPUT MACHINE {
    OPTION type AnalogueInput;
    OPTION VALUE 0;
    OPTION position 0;
}



# this output driver takes an analogue output or guarded analogue
# output such as an ANALOGDUALGUARD and drives the output in the
# range 0..32767.
#
# The zero position is the midpoint of the range: 16383
# An error is given if the input value 'NewValue' is out of range.

GLOBALDRIVER MACHINE {
  OPTION PERSISTANT true;
  EXPORT RW 32BIT MaxForward, MaxReverse, ZeroPos, MinForward, MinReverse;
  OPTION MaxForward 32767;
  OPTION MaxReverse 0;
  OPTION ZeroPos 16383;
  OPTION MinForward 19800;
  OPTION MinReverse 11800;
}


SIMFIXEDSPEEDMOTOR MACHINE fwd_sol, back_sol {
    OPTION VALUE 0; # motor drive value 0 .. 32767. Stationary is 16383
    OPTION Kspeed 100; # a scalar to convert value to speed in mm/sec
    OPTION scale 1000;
    OPTION zero 500; # the amount to move from centre before motion occurs
    OPTION max_value 32767;
    OPTION fixed_power 4000;
    OPTION hidden "scale";

    turning WHEN fwd_sol IS on OR back_sol IS on;
    stopped DEFAULT;

    RECEIVE fwd_sol.on_enter {  VALUE := max_value / 2 + fixed_power; }
    RECEIVE fwd_sol.off_enter {  VALUE := max_value / 2; }
    RECEIVE back_sol.on_enter { VALUE := max_value / 2 - fixed_power; }
    RECEIVE back_sol.off_enter { VALUE := max_value / 2; }
}

SIMOTOR MACHINE power_input {
    OPTION VALUE 16383; # motor drive power 0 .. 32767. Stationary is 16383
    OPTION Kspeed 2000; # a scalar to convert power to speed in mm/sec
    OPTION scale 1000;
    OPTION zero 4500; # the amount to move from centre before motion occurs
    OPTION max_value 32767;
    OPTION hidden "scale";
    OPTION PERSISTENT "true";
    
    # at power of 15000 (12000 after taking the zero point into account) 
    # the conveyor moves at 3000mm/sec
    # therefore Kspeed = 1 / 12 * 4  = 0.333. 
    # since we have no floating point, use 333 / 1000

    turning WHEN VALUE <= max_value/2-zero OR VALUE >= max_value/2+zero;
    stopped DEFAULT;
    
    #TBD. this RECEIVE works for analogue inputs but not for property changes.
    RECEIVE power_input.changed_enter {
        VALUE := power_input.VALUE;
    }
    
    COMMAND stop { VALUE := max_value/2; direction := 0; }
    COMMAND forward { IF (SELF IS stopped) { VALUE := max_value/2+zero; } }
    COMMAND backward { IF (SELF IS stopped) { VALUE := max_value/2-zero;} }
    COMMAND faster { VALUE := VALUE + 100 }
    COMMAND slower { VALUE := VALUE - 100 }
}
# the conveyor monitor accumulates movement of the conveyor since the last
# time the conveyor stopped. The following integrator sits in a forward
# or backward state for a time and recalculates position when it leaves.

SIMCONVEYORMONITOR MACHINE conveyor, motor, encoder {
    OPTION count 0;
    OPTION last 0;
    OPTION last_power 0; # the last recorded motor power
    OPTION time_step 10;
    OPTION hidden "last,time_step";
    OPTION velocity 0;
    
    # the accumulating state is used to trigger a position calculation at 
    # regular intervals
    accumulating WHEN (SELF IS forward OR SELF IS backward) AND TIMER>=time_step;
    
    forward WHEN conveyor IS moving AND motor.VALUE > (motor.max_value / 2 + motor.zero);
    backward WHEN conveyor IS moving AND motor.VALUE < (motor.max_value / 2 - motor.zero);
    zero WHEN conveyor IS stationary;
    unknown DEFAULT;
    
    ENTER zero{
        last := count; count := 0; last_power := 0; 
        velocity := 0;
        SET conveyor.is_forward TO off;
        SET conveyor.is_reverse TO off;
    }
    
    # we use the average power over the sampled interval, subtract the zero point for the motor and
    # scale the appropriately to convert the power level to a speed in ticks / sec
    ENTER forward { 
        last_power := motor.VALUE - motor.max_value/2 - motor.zero;
        SET conveyor.is_reverse TO off; SET conveyor.is_forward TO on 
     }
    LEAVE forward {
        CALL calc_velocity ON SELF;
        CALL calc_distance ON SELF;
    }
    ENTER backward {
        last_power := motor.VALUE - motor.max_value/2 + motor.zero;
        SET conveyor.is_forward TO off; SET conveyor.is_reverse TO on 
    }
    LEAVE backward { 
        CALL calc_velocity ON SELF;
        CALL calc_distance ON SELF;
    }
    RECEIVE calc_velocity {
        current := motor.VALUE - motor.max_value/2;
        IF (current > motor.zero) { current := current - motor.zero }
        ELSE {
            IF (current <= motor.zero && current >= -motor.zero) {
                current := 0;
            } ELSE { IF (current < -motor.zero) { current := current + motor.zero } }
        };
        velocity := (last_power + current) / 2 * motor.Kspeed / motor.scale;
    }
    RECEIVE calc_distance {
        count := velocity * TIMER / 1000;
        encoder.new_value := encoder.new_value + count;
    }
}

