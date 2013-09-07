# this file defines a timer machine that can be started
# and stopped by an independent machine. Timer machines
# like this are useful for measuring how long various 
# processes may take.

StartStopTimer MACHINE {

	ready INITIAL;
	started STATE;

	COMMAND stop { timer := TIMER; }
	COMMAND started { }
	TRANSITION started TO ready ON stop;
	TRANSITION ready TO started ON start;
}

# the TimedSwitch starts the timer when it turns on
# and stops the timer when it turns off.

TimedSwitch MACHINE timer {
  on STATE;
  off INITIAL;
  ENTER on { CALL start ON timer; }
  ENTER off { CALL stop ON timer; }
}

# the LightController turns on a light when the switch is on
# and turns it off when the switch is off

LightController MACHINE switch, light {

  on WHEN switch IS on;
  off DEFAULT;
  
  ENTER on { SET light TO on; }
  ENTER off { SET light TO off;}
}

# here is a bathroom light switch that is timed
# by the timed_switch; when the switch is turned off, 
# the duration is was left on can be found by looking
# at the property: timer1.timer

bathroom LightController timed_switch1, light;
timer1 StartStopTimer; 
timed_switch1 TimedSwitch timer1;
light FLAG;

