LightController MACHINE sensor, light {
	on WHEN SELF IS on AND TIMER < 5000;
	off DEFAULT; 
	RECEIVE sensor.on_enter { SEND reset TO SELF }
	# changes state while executing the reset command
	# this resets the timer
	resetting DURING reset { SET SELF TO on }
	ENTER on { SET light TO on }
	ENTER off { SET light TO off }
}

shed_switch FLAG(tab:Test);
shed_light FLAG(tab:Test);
shed_light_switch LightController(tab:Test) shed_switch, shed_light;


