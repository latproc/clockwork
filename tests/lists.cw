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

SetIntersect MACHINE {
	ready STATE;

p01 FLAG(usage:0,pos:1);
p02 FLAG(usage:0,pos:2);
p03 FLAG(usage:0,pos:3);
p04 FLAG(usage:0,pos:4);

all_positions LIST 3,2,1,4;
actions LIST p01,p02,p03,p04;
current_index_pos LIST 1,2;
reachable LIST; # list of positions reachable at this index pos
active LIST; # list of the flags that should activate

COMMAND GetAvailableGrabs {
	CLEAR reachable;
   	COPY COMMON 
		BETWEEN all_positions 
		AND current_index_pos 
		TO reachable;
   	COPY COMMON 
		BETWEEN actions
		AND reachable
		TO active
		USING pos;
}

}
intersect SetIntersect;

SetUnion MACHINE {
	ready STATE;

front_shallow LIST 4,5,6;
front_deep LIST 10,11,12;

front LIST;

COMMAND union {
	CLEAR front;
   	COPY ALL
		IN front_shallow
		OR front_deep
		TO front
		USING dummy;
}

}
union SetUnion;

SetDifference MACHINE {
	hat FLAG;
	tree FLAG;
	frog FLAG;

	all LIST hat,tree,frog;
	forest LIST tree,frog;
	odd LIST;

	ENTER INIT {
		COPY DIFFERENCE BETWEEN all AND forest TO odd;
	}
}
diff SetDifference;
