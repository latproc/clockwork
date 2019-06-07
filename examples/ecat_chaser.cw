# here is a program that produces a light chaser pattern on a 
# list of lights.

EK1814 MODULE (position:0);
Outputs MODULE (position:2);
out1 POINT EK1814,0;
out2 POINT EK1814,1;
out3 POINT EK1814,2;
out4 POINT EK1814,3;

group1 LIST out1,out2,out3,out4;

in1 POINT EK1814, 4;
in2 POINT EK1814, 5;
in3 POINT EK1814, 6;
in4 POINT EK1814, 7;

/*
EL3151 MODULE (position:5);
AInSettings MACHINE { IDLE INITIAL; }
ain_settings AInSettings;
ain ANALOGINPUT EL3151, 10, ain_settings;

SpeedSettings MACHINE { idle INITIAL; }
a1speed_settings SpeedSettings;
a1speed RATEESTIMATOR ain,a1speed_settings;

out5 POINT Outputs,0;
out6 POINT Outputs,1;
out7 POINT Outputs,2;
out8 POINT Outputs,3;

out9 POINT Outputs,4;
out10 POINT Outputs,5;
out11 POINT Outputs,6;
out12 POINT Outputs,7;
group2 LIST out5, out6, out7, out8, out9, out10, out11, out12;
*/

Cell MACHINE output, left, right {
	OPTION tab test;
	stopping FLAG;
	#error WHEN SELF IS error 
	#	OR (right IS NOT on AND right IS NOT off AND right IS NOT error)
	#	OR (left IS NOT on AND left IS NOT off AND left IS NOT error);
	off WHEN stopping IS on;
	turningOn WHEN SELF IS waiting AND TIMER>=10;
	waiting WHEN left IS on AND output IS off;
	on WHEN output IS on AND right IS NOT on;
    off DEFAULT;
	off INITIAL;
    COMMAND start { SET stopping TO off; SET output TO on }
    COMMAND reset { SET stopping TO off; }
    COMMAND stop { SET stopping TO on }
	ENTER turningOn { SET output TO on; }
	#LEAVE on { SET right TO on; }
	ENTER off { SET output TO off; }
}

led01 Cell out1, led04, led02;
led02 Cell out2, led01, led03;
led03 Cell out3, led02, led04;
led04 Cell out4, led03, led01;

leds LIST led01, led02, led03, led04;

/*
led05 Cell out5, led12, led06;
led06 Cell out6, led05, led07;
led07 Cell out7, led06, led08;
led08 Cell out8, led07, led09;

led09 Cell out9, led08, led10;
led10 Cell out10, led09, led11;
led11 Cell out11, led10, led12;
led12 Cell out12, led11, led05;

#led05 Cell led04, led06;
#led06 Cell led05, led01;
*/

