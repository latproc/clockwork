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

  waiting WHEN outputs DISABLED;
  on WHEN ALL outputs ARE on AND outputs ENABLED;
  off DEFAULT;
  
  COMMAND turnOn { SEND turnOn TO outputs }
  COMMAND turnOff { SEND turnOff TO outputs }
  COMMAND enable { ENABLE outputs; }
  COMMAND disable { DISABLE outputs; }
  
}
controller Controller lights;

one VARIABLE 1;
two VARIABLE 2;
three VARIABLE 3;
four VARIABLE 4;

numbers LIST two,four,three,one;

Sorter MACHINE input {

COMMAND sort { LOG "sorting"; SORT input }

}
sorter Sorter numbers;


Queue MACHINE {
queue LIST 1,2,3;
	nonempty WHEN queue IS nonempty;
	empty DEFAULT;

	ENTER INIT { CLEAR queue; }
}
q Queue;

digits LIST 0,1,2,3,4,5,6,7,8,9;
DigitTest MACHINE list {
    OPTION sz 0;
    changed WHEN sz != SIZE OF list;
    done WHEN 5 == LAST OF list;
    nonempty WHEN SIZE OF list > 0;
    empty DEFAULT;
    ENTER INIT { xx := LAST OF list; }
    ENTER changed { sz := SIZE OF list; LOG "items in list: " + sz; }
    ENTER nonempty { val := TAKE LAST FROM list; LOG "Value popped: " + val; }
}
dt DigitTest digits;
dt2 DigitTest numbers;
