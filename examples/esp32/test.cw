
led FLAG;
pulser Pulse (delay:50) led;
#analog MODULE(position:1);
#out ANALOGOUTPUT analog, 0;
out VARIABLE 0;
ramp Ramp pulser, out;


