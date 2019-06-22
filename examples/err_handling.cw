# This file demonstrates some of the error handling features
# of clockwork. 

# A simple flag-style machine

Flag MACHINE { on STATE; off INITIAL; }

# This example does not define any error handling and 
# generates an error.
SimpleBadSet MACHINE {
	f Flag;

	# each step of the following will execute even though the SET fails
	COMMAND go { 
		LOG "about to try to set f to an invalid state";
		SET f TO unknown;
		LOG "finished trying to set f to an invalid state";
	}

} 
badset BadSet;

# This example uses error handling to detect an error
# and abort execution
ErrorCatchBadSet MACHINE {
	f Flag;

	# because the following throw the error from the SET, the 
	# command aborts and the second LOG message will not execute
	# instead, the setFailed handler will be executed
	COMMAND go { 
		LOG "about to try to set f to an invalid state";
		SET f TO unknown ON ERROR setFailed;
		LOG "finished trying to set f to an invalid state";
	}

	CATCH setFailed {
		LOG "Failed to set flag f to state unknown";
	}
}


