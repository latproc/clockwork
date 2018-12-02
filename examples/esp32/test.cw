
button FLAG;
led FLAG;
pulser Pulse (delay:50) led;
aout VARIABLE 0;
ramp Ramp pulser, aout;
d_button DebouncedInput button;
speed_select SpeedSelect d_button, pulser;

