# Test whether nested machines are properly scheduled for 
# automatic state changes when timers are involved.

Master MACHINE raw_input {

input DebouncedInput raw_input;

running STATE;
idle INITIAL;

RECEIVE input.on_enter { SET SELF TO running; }
RECEIVE input.on_leave { SET SELF TO idle; }

}

DebouncedInput MACHINE in {

on WHEN in IS on AND in.TIMER >= 2000;
off DEFAULT;

}

button FLAG;
master Master button;

# Test whether state changes on items on a list generate
# events that cause machines dependent on the list to update

a FLAG;
b FLAG;
flags LIST a,b;
Holder MACHINE {
	list LIST;
	on WHEN SIZE OF list > 0 AND ANY list ARE on;
	off DEFAULT;

	ENTER INIT { PUSH a TO list; PUSH b TO list; }
}

holder Holder;
