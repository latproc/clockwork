# here is a program that produces a light chaser pattern on a 
# list of lights.

Outputs MODULE (position:0);
out1 POINT Outputs,0;
out2 POINT Outputs,1;
out3 POINT Outputs,2;
out4 POINT Outputs,3;

Cell MACHINE output, left, right {
	OPTION tab test;
    on WHEN right IS off 
		AND (SELF IS on OR left IS on AND SELF IS off AND left.TIMER >= 200);
    off DEFAULT;
    starting DURING start { SET SELF TO on }
	ENTER on { SET output TO on; }
	ENTER off { SET output TO off; }
}

led01 Cell out1, led04, led02;
led02 Cell out2, led01, led03;
led03 Cell out3, led02, led04;
led04 Cell out4, led03, led01;
#led05 Cell led04, led06;
#led06 Cell led05, led01;

