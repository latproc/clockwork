# here is a program that produces a light chaser pattern on a 
# list of lights.

EK1814Outputs MODULE (position:0);
Outputs MODULE (position:2);
out1 POINT EK1814Outputs,0;
out2 POINT EK1814Outputs,1;
out3 POINT EK1814Outputs,2;
out4 POINT EK1814Outputs,3;

group1 LIST out1,out2,out3,out4;

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
	#error WHEN SELF IS error 
	#	OR (right IS NOT on AND right IS NOT off AND right IS NOT error)
	#	OR (left IS NOT on AND left IS NOT off AND left IS NOT error);
    #on WHEN right IS off AND SELF IS on AND TIMER <= 10;
		#AND (SELF IS on OR left IS on AND SELF IS off AND left.TIMER >= 1000);
	turningOn WHEN left IS on AND output IS off;
	on WHEN output IS on AND right IS NOT on;
    off DEFAULT;
	off INITIAL;
    COMMAND start { SET output TO on }
	ENTER turningOn { SET output TO on; }
	#LEAVE on { SET right TO on; }
	ENTER off { SET output TO off; }
}

led01 Cell out1, led04, led02;
led02 Cell out2, led01, led03;
led03 Cell out3, led02, led04;
led04 Cell out4, led03, led01;

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

