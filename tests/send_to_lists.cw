# verify that messages sent to a list are received by all items exactly once

Receiver MACHINE {
	done STATE;
	error STATE;
	waiting INITIAL;
	TRANSITION waiting TO done ON finished;
	TRANSITION done TO error ON finished;
}

target1 Receiver;
target2 Receiver;
target3 Receiver;
nested_list LIST target3, target2;
target_list LIST target1, target2, nested_list;
all_targets LIST target1, target2, target3;

SenderTest MACHINE targets, flattened_targets {
	error WHEN ANY flattened_targets ARE error OR SELF IS error;
	ready WHEN ALL flattened_targets ARE waiting;
	done WHEN (SELF IS done OR SELF IS working) AND ALL flattened_targets ARE done;
	working WHEN ANY flattened_targets ARE done;

	COMMAND go WITHIN ready {
		SEND finished TO targets;
	}

	ENTER INIT {
	    DISABLE targets;
    	ENABLE targets;
    	LOG "reset targets"
	}
}

TestScript MACHINE sender {
    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
    	DISABLE sender;
    	ENABLE sender;
    	WAITFOR sender IS ready;
    	SEND go TO sender;
    	WAITFOR sender IS done;
    }
}


Driver MACHINE test {
    script TestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}

sender SenderTest target_list, all_targets;
driver Driver sender;
