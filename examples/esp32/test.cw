# this is how we can instantiate objects for the esp32 demo

esp32 ESP32; # CPU defined in the runtime support
gateway32 OLIMEX_GATEWAY32 esp32; # board defined in the runtime support

# gateway32 MODULE (cpu: ESP32, board: OLIMEX_GATEWAY32); alternative setup, not functional yet

button INPUT gateway32, gateway32.BUT1;
led OUTPUT gateway32, gateway32.LED;
aout ANALOGOUTPUT gateway32, gateway32.GPIO16 gateway32.cpu.GPIO16;

pulser Pulse (delay:50) led;
ramp Ramp pulser, aout;
d_button DebouncedInput button;
speed_select SpeedSelect d_button, pulser;

