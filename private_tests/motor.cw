/*
# demonstrates the use of a COUNTERRATE machine to 
# compute speed from a series of position values

MotionSimulator MACHINE speed {
	OPTION pos 0;
	OPTION rate 6;
	updating WHEN SELF IS waiting AND TIMER > 5;
	waiting DEFAULT;
	
	ENTER updating { 
	    pos := pos + rate; 
	    speed.VALUE := pos; # provide a position value
	    VALUE := speed.VALUE; # get back a speed
	    LOG "speed: " + VALUE + " pos: " + speed.position;
	}
}

SpeedMonitor MACHINE encoder {
	OPTION speed 0;
	OPTION pos 0;
	updating WHEN encoder.VALUE != speed;
	waiting DEFAULT;
	ENTER updating { 
    	speed := encoder.VALUE; 
    	pos := encoder.position; 
    	msg :=  "speed: " + speed + " pos: " + pos; 
    	SET SELF TO waiting;
	}
}

enc COUNTERRATE;
motor MotionSimulator enc;
speed_monitor SpeedMonitor enc;
*/


# simulated hardware IO
motor_power SIMANALOGUEOUTPUT (tab:motor);  # analog output to the motor this changes state to generate messages for the SIMOTOR
conveyor_pos VARIABLE(tab:motor) 0;        # quadrature input for the encoder

# simulation drivers to provide reasonable data at the simulated hardware
conveyor_encoder SIMENCODER(tab:motor) conveyor_pos;   # generates simulated position for the quadrature encoder
sim_motor SIMOTOR(tab:motor, Kspeed:150) motor_power;
sim_conveyor SIMCONVEYOR(tab:motor) sim_motor;         # tracks whether the conveyor is moving and if it is fwd/rev
sim_monitor SIMCONVEYORMONITOR(tab:motor)              # computes position for the encoder based on motor speed
    sim_conveyor, sim_motor, conveyor_encoder;  

# control modules
rate_settings VARIABLE(tab:motor) 0; # dummy settings data
conveyor_speed RATEESTIMATOR(tab:motor) conveyor_pos, rate_settings;  # rate of motion based

driver_config GLOBALDRIVER(tab:motor);
#motor_driver ANALOGOUTPUTDRIVER(tab:motor) driver_config, motor_power;
#ramp_config RAMPCONFIGURATION(tab:motor);
#ramp_controller ANALOGPOWERRAMP(tab:motor) ramp_config, motor_driver;
#conveyor_control STANDARDCONTROL(tab:GrabOutputs);

speed_config RAMPSPEEDMACHINECONFIGURATION(tab:motor, min_update_time:50);
forward_config RAMPSPEEDCONFIGURATION(tab:motor, 
        TravelAllowance:300, 
        StoppingDistance:600,
        PowerFactor:1000);
reverse_config RAMPSPEEDCONFIGURATION(tab:motor, 
        TravelAllowance:300, 
        StoppingDistance:600,
        SlowSpeed:-100, 
        FullSpeed:-1000, 
        PowerFactor:500);
speed RAMPSPEEDCONTROLLER(tab:motor) conveyor_control, speed_config, driver_config, 
    forward_config, reverse_config, ramp_controller, conveyor_speed;

#controller ANALOGSPEEDCONTROLLER(tab:motor), speed_config, motor_driver, conveyor_speed;

