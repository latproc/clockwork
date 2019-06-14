# tests of exceptions
#


# A logger machine for the purpose of the test
Logger MACHINE { RECEIVE test { LOG "test message received"; } }


# Test whether commands work on disabled machines.
DisabledReceiverTest MACHINE {

	# setup a logger and disable it at INIT
	logger Logger;

	COMMAND test { 
		DISABLE logger;
		SEND test TO logger; # SEND will succeed
		LOG SELF.NAME + " this message, after SEND should appear";
	}


	COMMAND test_call { 
		DISABLE logger;
		CALL test ON logger; # CALL will fail
		LOG SELF.NAME + " this message, after CALL should not appear";
	}


	COMMAND test_on_error { 
		DISABLE logger;
		CALL test ON logger ON ERROR DisabledMessageTargetException; # CALL will fail
		LOG SELF.NAME + " this message, after CALL should not appear";
	}
	RECEIVE DisabledMessageTargetException {
		LOG "target was disabled"
	}

	COMMAND test_nested {
		DISABLE logger;
		IF (1 == 1) {
			LOG "calling test on logger within IF block";
			CALL test ON logger ON ERROR NestedTestError; # CALL will fail
			LOG "This message after a failed call in IF block should not appear"
		};
		LOG SELF.NAME + " this message, after CALL after IF should not appear";
	}
	RECEIVE NestedTestError {
		LOG "Nested CALL test aborted";
	}
}
drTest DisabledReceiverTest;


AbortTest MACHINE {
	OPTION count 0;

	tick WHEN SELF IS idle AND TIMER >= 1000;
	idle DEFAULT;

ENTER tick {
	count := count + 1;
	LOG "This message before an IF should be displayed" + count + "/3";
	IF (count >= 3) { 
		#LOG "This tick message before a THROW should be displayed";
		THROW overrun ;
		LOG "This tick message should not be displayed";
	};
	LOG "This message after an IF should only be displayed if the tick message has not been displayed"
}

ENTER INIT {
	count := 0;
	LOG "This message should be displayed";
	SEND abort_test TO SELF;
	RETURN;
	LOG "This message should not be displayed";
}

RECEIVE abort_test {
	LOG "Beginning test of abort";
	ABORT;
	LOG "This idle message should not be displayed";
}

CATCH overrun {
	LOG "Overrun detected";
	DISABLE SELF;
}

}

OverRunMonitor MACHINE {
	RECEIVE overrun {
		LOG "Monitor detected Overrun";
	}
}

abort_test AbortTest;
monitor OverRunMonitor;

