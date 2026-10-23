# VELOCITY is a derived machine: it consumes one raw source VALUE/IOTIME pair.
# prod-experimental-mqtt-fix keeps these outputs internal to CW.

source VARIABLE (tab:Input, PERSISTENT:true) 0;
axis_velocity VELOCITY (WindowMs:200, MinSamples:4, Scale:0.000192,
                        StaleMs:250, MaxAbsValue:1000) source;
