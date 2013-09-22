# here is a program that produces a light chaser pattern on a 
# list of lights.

Cell MACHINE left, right {
    on WHEN right IS off AND (SELF IS on OR left IS on AND SELF IS off AND left.TIMER >= 1000);
    off DEFAULT;
    starting DURING start { SET SELF TO on }
}

led01 Cell led06, led02;
led02 Cell led01, led03;
led03 Cell led02, led04;
led04 Cell led03, led05;
led05 Cell led04, led06;
led06 Cell led05, led01;

