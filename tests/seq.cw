# this script show how a machine can be build to monitor the state changes of another machine
# minor states can be ignored by adding them to a list of minor states

# This implementation only works for machines that linger in each state for a reasonable
# time (eg 10ms or so) but it has the advantage that the monitor does not have to be 
# coded specially for states of the machine under test
#
SequenceMonitor MACHINE other, sequence {

	error WHEN SELF IS error;

	done WHEN SIZE OF sequence <= 1;
	ok WHEN other.STATE == ITEM 0 OF sequence;
	checking WHEN other.STATE == ITEM 1 OF sequence;
	error DEFAULT;
	ENTER error { LOG other.NAME + " is in unexpected state " + other.STATE 
		+ " expected " + ITEM 0 OF sequence + " or next state: " + ITEM 1 OF sequence; }

	TRANSITION ok TO checking USING cycle;

	COMMAND cycle { x := TAKE FIRST FROM sequence; PUSH x TO sequence; }
}

test_flag FLAG;
flag_states LIST "off", "on";

flag_monitor SequenceMonitor test_flag, flag_states;

Cycle MACHINE {
	one INITIAL; two STATE; three STATE; four STATE;
	TRANSITION one TO two ON next;
	TRANSITION two TO three ON next;
	TRANSITION three TO four ON next;
	TRANSITION four TO one ON next;
}
cycle Cycle;
cycle_states LIST "one", "two", "three", "four";
cycle_monitor SequenceMonitor cycle, cycle_states;

