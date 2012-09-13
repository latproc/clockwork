MotionSensor MACHINE sensor_input {
	active WHEN sensor_input IS on;
	idle DEFAULT;
}
motion_osensor MotionSensor sensor;
sensor FLAG;
switch FLAG;


LightController MACHINE sensor, light_switch {
	on WHEN sensor IS on;
	off DEFAULT;

	ENTER on { SET light_switch TO on }
	ENTER off { SET light_switch TO off }
}
controller LightController sensor, switch;

