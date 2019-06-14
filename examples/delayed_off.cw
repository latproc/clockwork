# this machine turns on as soon as it sees the input come on 
# and turns off after 5s. Each time the sensor changes
# back to on the timer resets.

DelayedOff MACHINE sensor {
	on WHEN SELF IS on AND TIMER < 5000;
	off DEFAULT; 
	RECEIVE sensor.on_enter { SEND reset TO SELF }
	resetting DURING reset { SET SELF TO on }
}
in FLAG;
delay DelayedOff in;
