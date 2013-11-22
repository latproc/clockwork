/* lists.cw - play with various list functions
 */

BOOL MACHINE { true STATE ; false INITIAL; }

Light MACHINE output {
    OPTION turn_on_delay 2000;
	turning_on BOOL;
	on WHEN output IS on;
	waitingOn WHEN SELF IS delayingOn AND TIMER >= turn_on_delay;
	delayingOn WHEN turning_on IS true;
	off DEFAULT;
	RECEIVE turnOn { SET turning_on TO true; }
	RECEIVE turnOff { SET turning_on TO false; SET output TO off; }
	ENTER INIT { SET output TO off; }
    ENTER on { SET turning_on TO false }
	ENTER off { SET turning_on TO false }
	ENTER waitingOn { SET output TO on; }
}
gpio1 FLAG;
gpio2 FLAG;

led01 Light(turn_on_delay:5000) gpio1;
led02 Light gpio2;
lights LIST led01, led02;

Controller MACHINE outputs {

  on WHEN ALL outputs ARE on;
  off DEFAULT;
  
  COMMAND turnOn { SEND turnOn TO outputs }
  COMMAND turnOff { SEND turnOff TO outputs }
  COMMAND enable { ENABLE outputs; }
  COMMAND disable { DISABLE outputs; }
  
}
controller Controller lights;

