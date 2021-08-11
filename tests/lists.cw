/* lists.cw - play with various list functions
 */


/* check whether lists can turn members on and off */
Flags LIST f1, f2, f3;
f1 FLAG;
f2 FLAG;
f3 FLAG;

TestListCommands MACHINE list {
	COMMAND TurnOn { SEND turnOn TO list; }
	COMMAND TurnOff { SEND turnOff TO list; }
}
test_list_commands TestListCommands Flags;


BOOL MACHINE { true STATE ; false INITIAL; }

/* A light controller iwth the ability to receive turnOn/turnOff commands
 */
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
/* the lights are connected to general purpose io points, simulated as flags */
gpio1 FLAG;
gpio2 FLAG;

led01 Light(turn_on_delay:5000) gpio1;
led02 Light gpio2;
lights LIST led01, led02;

/* a light controller for a list of lights, this controller
	can enable/disable and turn-on or turn-off  the list of
	lights all at once
 */
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

/* Example, using VARIABLEs as objects in a list */

one VARIABLE 1;
two VARIABLE 2;
three VARIABLE 3;
four VARIABLE 4;

numbers LIST two,four,three,one;

number_list LIST two,four,three,one;

/* A sorting machine to sort its parameter into 
	numeric order
*/
Sorter MACHINE input {
	COMMAND sort { LOG "sorting"; SORT input BY PROPERTY VALUE }
}
sorter Sorter number_list;

/* A test of what happens when a COPY is done an no values match */
Copier MACHINE input, output {
	OPTION VALUE 3;
	COMMAND copy { 
		CLEAR output; 
		COPY ALL FROM input TO output WHERE input.ITEM == VALUE;
	}
}
copied LIST;
copier Copier number_list, copied;

/* and example of how to use a LIST for FIFO queue operations */
Queue MACHINE {
queue LIST 1,2,3;
	nonempty WHEN queue IS nonempty;
	empty DEFAULT;

	ENTER INIT { CLEAR queue; }
}
q Queue;

/* here is an alternative example that uses a list for a FIFO */
AltQueue MACHINE {
queue LIST;
	nonempty WHEN SIZE OF queue > 0;
	empty DEFAULT;

	ENTER nonempty { x := TAKE FIRST FROM queue; LOG "got: " + x }
	ENTER empty { LOG "empty" }
}
qa AltQueue;

/* Playing with a list of digits */
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

/* Demonstration of set functions using LISTs */

/* Set intersection */
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

# string lists

names LIST "bill", "ted";
Logger MACHINE names {
	task_done STATE;
	working WHEN SIZE OF names > 0;
	idle DEFAULT;

	ENTER working { 
		val := TAKE FIRST FROM names; 
		LOG val; 
		SET SELF TO task_done;
		IF ( val == "ted" ) { INCLUDE "mary" IN names; }
	}
}
logger Logger names;

TestMatch MACHINE {
	queue LIST;
	yes WHEN (FIRST OF queue) MATCHES `test`;
	no DEFAULT;

	ENTER yes { tmp := TAKE FIRST FROM queue; LOG tmp; }
}
test_match TestMatch;

BitsetComparisonTest MACHINE {
	l1 LIST f1,f2;
	l2 LIST f3,f4;

	f1 FLAG; f2 FLAG; f3 FLAG; f4 FLAG;

	ok WHEN BITSET FROM l1 == BITSET FROM l2;
	failed DEFAULT;

	ENTER INIT { SET f2 TO on; SET f4 TO on; }
}
bitset_comparison_test BitsetComparisonTest;

a FLAG; b FLAG; c FLAG;
l LIST a,b,c;
PopAndPushTest MACHINE list {
	l2 LIST;

	ENTER INIT { 
		s := ITEM 0 OF list;
		PUSH s TO l2;
		s := ITEM 1 OF list;
		PUSH s TO l2;
	}
}
pop_and_push_test PopAndPushTest l;


sat1 FLAG;
sat2 FLAG;
sat3 FLAG;
sat_list LIST sat1, sat2, sat3;

SelectAllTest MACHINE list {
	temp LIST;
	two LIST;
	saved LIST;
	COMMAND run { 
		CLEAR temp;
		CLEAR two;
		COPY ALL FROM list TO temp WHERE list.ITEM IS on;
		COPY 2 FROM list TO two WHERE list.ITEM IS off;
		MOVE ALL FROM temp TO saved;
	}
}
select_all_test SelectAllTest sat_list;

it1 FLAG(size:3);
it2 FLAG(size:4);
it3 FLAG(size:3);
it4 FLAG(size:2);
sizes LIST it1,it2,it3,it4;
itm1 FLAG(length:1);
itm2 FLAG(length:2);
itm3 FLAG(length:3);
itm4 FLAG(length:4);
items LIST itm1, itm2, itm3, itm4;

IntersectionTest MACHINE source, selector {
    threes LIST;
    result LIST;
    COMMAND run {
        CLEAR result;
        COPY ALL FROM source TO result SELECT USING selector WHERE source.ITEM.length == selector.ITEM.size;
		MOVE 2 FROM result TO threes SELECT USING selector WHERE result.ITEM.length+-1 == selector.ITEM.size+-1 &&  result.ITEM IS on;
    }
}
intersection_test IntersectionTest items, sizes;

dept1 FLAG;
dept2 FLAG;
dept3 FLAG;
depts LIST dept1,dept2,dept3;

DependancyTest MACHINE items {

	on WHEN ALL items ARE on;
	off DEFAULT;

	COMMAND clear { CLEAR items; }
	COMMAND setup { 
		PUSH dept1 TO items; 
		PUSH dept2 TO items; 
		PUSH dept3 TO items; 
	}
}
dependancy_test DependancyTest depts;


myf1 FLAG(x:1);
myf2 FLAG(x:2);
aa LIST myf1, myf2;
testcopy MACHINE list {
    curr LIST;
	a WHEN ALL curr ARE on;
	b DEFAULT;

	ENTER a {  z := TAKE FIRST FROM curr; z.a := 7; }
	ENTER INIT { COPY 1 FROM list TO curr; }
}
tc testcopy aa;

StringList LIST "a,b,c", "one two three", "house rabbit tree";

pop_and_push_strings PopAndPushTest StringList;

Other MACHINE a,b {
  list LIST a,b;
}

CopyFromOther MACHINE other {
  local LIST;
  
  ENTER INIT {
    WAIT 2000;
    COPY ALL FROM other.list TO local;
    LOG "local list size: " + (SIZE OF local);
  }
}
ff1 FLAG;
ff2 FLAG;
other Other ff1,ff2;
copy_from_other CopyFromOther other;


