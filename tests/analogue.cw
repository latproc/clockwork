# this program is used to define a test rig that includes
# a couple Beckhoff devices on the EtherCAT(TM) bus. 
#
# ANALOGINPUT is a new machine class and its usage is changing.

MODULES {
	EK1814 (position:0);
	EL3051 (position:1);
}

out1 POINT EK1814, 0;
out2 POINT EK1814, 1;
out3 POINT EK1814, 2;
out4 POINT EK1814, 3;
in1 POINT EK1814, 4;
in2 POINT EK1814, 5;
in3 POINT EK1814, 6;
in4 POINT EK1814, 7;

inputs LIST in1, in2, in3, in4, pressure, pressure_Underrange, pressure_Overrange;
outputs LIST out1, out2, out3, out4;

pressure_Underrange POINT EL3051, 0;
pressure_Overrange POINT EL3051, 1;
#pressure_Limit1 POINT EL3051, 2;
#pressure_Limit2 POINT EL3051, 3;
pressure_Error POINT EL3051, 4;
#pressure_TxPDO_State POINT EL3051, 7;
#pressure_TxPDO_Toggle POINT EL3051, 8;
ANALOGIN_SETTINGS MACHINE { idle INITIAL; }
pressure_settings ANALOGIN_SETTINGS;
pressure ANALOGINPUT EL3051, 9, pressure_settings;

SpeedSettings MACHINE { idle INITIAL; }
a1speed_settings SpeedSettings;
pressure_rate RATEESTIMATOR pressure,a1speed_settings;
