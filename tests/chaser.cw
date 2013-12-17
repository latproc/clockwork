# here is a program that produces a light chaser pattern on a 
# list of lights.

Cell MACHINE left, right {
    on WHEN right IS off AND (SELF IS on OR left IS on AND SELF IS off AND left.TIMER >= 1000);
    off DEFAULT;
    starting DURING start { SET SELF TO on }
}

led01 Cell(tab:cells) led06, led02;
led02 Cell(tab:cells) led01, led03;
led03 Cell(tab:cells) led02, led04;
led04 Cell(tab:cells) led03, led05;
led05 Cell(tab:cells) led04, led06;
led06 Cell(tab:cells) led05, led01;

