# In an apartment block, a push button at each floor turns on the stairwell
# lights. After the lights have been on for five minutes they are dimmed.
# While dimmed, if any button is pressed the timer is reset so the 
# lights remain on for a further five minutes. If the lights remain
# dimmed for two minutes they are turned off.

LightController MACHINE light_bulbs {
	# the following 'WHEN' rules are executed in the order they
	# are expressed and evaluation stops once a rule evaluates to true
	
	OPTION on_timer 600000; # five minutes (600000ms)
	OPTION dim_timer 120000; # two minutes (120000ms)

	# the internal timer 'TIMER' resets whenever a state is entered

	off WHEN SELF IS dimmed AND TIMER >= dim_timer; 
	dimmed WHEN SELF IS on AND TIMER >= on_timer; 

	on WHEN SELF IS on; # stay in the on state until pulled out
	dimmed WHEN SELF IS dimmed; # stay in the dimmed state until pulled out

	off DEFAULT;

	ENTER on { SEND turnOn TO light_bulbs; }
	ENTER dimmed { SEND dim TO light_bulbs; }
	ENTER off { SEND turnOff TO light_bulbs; }

	# this controller receives a message to turn on if a switch is pressed
	turningOn DURING turnOn { SET SELF TO on; }
}

LightSwitch MACHINE input, controller {
	on WHEN input IS on;
	off DEFAULT;
	ENTER on { SEND turnOn TO controller }
}

# light level controller with three states, on, dimmed, off 
DimmerLight MACHINE output {
	off INITIAL;
	on STATE;
	dimmed STATE;

	TRANSITION ANY TO on ON turnOn;
	TRANSITION on TO dimmed ON dim;
	TRANSITION ANY TO off ON turnOff;

	ENTER on { output.VALUE := 32767; }
	ENTER dimmed { output.VALUE := 22937; } # dimmed to 70%
	ENTER off { output.VALUE := 0; }
}

# wiring configuration for the apartment control program
# note that machine definitions and configuration items
# can occur in any order, before or after the related 
# machine definitions

controller LightController lights;
lights LIST floor1_light, floor2_light, floor3_light, floor4_light;

floor1_light DimmerLight aout1;
floor2_light DimmerLight aout2;
floor3_light DimmerLight aout3;
floor4_light DimmerLight aout4;

floor1_button LightSwitch in1, controller;
floor2_button LightSwitch in2, controller;
floor3_button LightSwitch in3, controller;
floor4_button LightSwitch in4, controller;

# the hardware configuration for the above program
# (using Beckhoff EtherCAT I/O modules)
EK1814 MODULE (position:0);
EL4124 MODULE (position:1);

# light switches located on each floor
in1 POINT EK1814, 4;
in2 POINT EK1814, 5;
in3 POINT EK1814, 6;
in4 POINT EK1814, 7;

# light circuits located on each floor
aout1 ANALOGOUTPUT EK1814,0;
aout2 ANALOGOUTPUT EK1814,1;
aout3 ANALOGOUTPUT EK1814,2;
aout4 ANALOGOUTPUT EK1814,3;




