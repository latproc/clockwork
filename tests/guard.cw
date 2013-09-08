# Demonstrates the idea of a guarded input
#
# A Guard can be used to apply additional logic to another
# device. For example, a security light might turn on whenever
# motion is detected by the sensor. We might guard that with
# a light sensor that prevents the security light from operating
# if it is daylight.
#
GuardedInput MACHINE input, guard {
    on WHEN input IS on AND guard IS on;
    off DEFAULT;
}

SensorLight MACHINE sensor { 
    on WHEN SELF IS on and TIMER < 1000 OR sensor IS on;
    off DEFAULT;
}

light_sensor FLAG; # a simulation of a sensor that turns on when there is daylight
motion_sensor FLAG; # simulation of a motion sensor
guarded_motion_sensor GuardedInput motion_sensor, light_sensor;

automatic_light SensorLight guarded_motion_sensor;

# We tend to generalise the above concept and define a general
# purpose guard that uses the 'true' and 'false' states. We
# can define them such that either state is the default, depending
# how the conditions might be specified.

Guard MACHINE raw_input {
  false WHEN raw_input IS on;
  true DEFAULT;
}

