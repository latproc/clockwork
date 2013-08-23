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

pressure_Underrange STATUS_FLAG EL3051, 0, 0;
pressure_Overrange STATUS_FLAG EL3051, 0, 1;
#pressure_Limit1 STATUS_FLAG EL3051, 0, 2;
#pressure_Limit2 STATUS_FLAG EL3051, 0, 3;
pressure_Error STATUS_FLAG EL3051, 0, 4;
pressure_TxPDO_State STATUS_FLAG EL3051, 0, 7;
pressure_TxPDO_Toggle STATUS_FLAG EL3051, 0, 8;
pressure ANALOGINPUT EL3051, 0, 9;
