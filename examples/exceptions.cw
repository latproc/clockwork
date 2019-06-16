# tests of exceptions
#


# A logger machine for the purpose of the test
Logger MACHINE { RECEIVE test { LOG "test message received"; } }


# Test whether commands work on disabled machines.
DisabledReceiverTest MACHINE {

    working STATE;
	idle INITIAL;
	idle WHEN SELF IS idle || SELF IS working; # drop here when finished a command
	
	# setup a logger and disable it at INIT
	logger Logger;

	test_send_to_disabled_ok STATE;
	COMMAND test_send_to_disabled {
		SET SELF TO working;
		DISABLE logger;
		SEND test TO logger; # SEND will succeed
		LOG SELF.NAME + " this message, after SEND should appear";
		SET SELF TO test_send_to_disabled_ok;
	}

	# try to call a command on a disabled machine; this will fail but the only
	# way to verify this within the script is to send a messsage to self, which will
	# be picked up after the current method completes but if the CALL does not abort,
	# change state to prevent receiving the message. TODO: need a simpler way.

	test_call_to_disabled_ok STATE;
	COMMAND test_call_to_disabled {
		SET SELF TO working; 
		DISABLE logger;
		SEND BadCall TO SELF;
		CALL test ON logger; # CALL will fail
		LOG SELF.NAME + " this message, after CALL should not appear";
		SET SELF TO idle;
	}
	RECEIVE BadCall {
		LOG "test_call_to_disabled successfully aborted";
		SET SELF TO test_call_to_disabled_ok;
	}

	test_on_error_raises_ok STATE;
	COMMAND test_on_error_raises { 
		SET SELF TO working;
		DISABLE logger;
		CALL test ON logger ON ERROR DisabledMessageTargetException; # CALL will fail
		LOG SELF.NAME + " this message, after CALL should not appear";
	}
	RECEIVE DisabledMessageTargetException {
		SET SELF TO test_on_error_raises_ok;
		LOG "call to disabled raises"
	}

    test_on_error_aborts_ok STATE;
	COMMAND test_on_error_aborts { 
		SET SELF TO working;
		DISABLE logger;
		CALL test ON logger ON ERROR OnErrorAbortTestException; # CALL will fail
		LOG SELF.NAME + " this message, after CALL should not appear";
	}
	RECEIVE OnErrorAbortTestException {
		LOG "on error aborts";
		SET SELF TO test_on_error_aborts_ok;
	}

    test_nested_raises_ok STATE;
	COMMAND test_nested_raises {
		SET SELF TO working;
		DISABLE logger;
		IF (1 == 1) {
			LOG "calling test on logger within IF block";
			CALL test ON logger ON ERROR NestedTestError; # CALL will fail
			LOG "This message after a failed call in IF block should not appear"
		};
		LOG SELF.NAME + " this message, after CALL after IF should not appear";
	}
	RECEIVE NestedTestError {
		LOG "Nested CALL test raises ok";
		SET SELF TO test_nested_raises_ok;
	}
}
drTest DisabledReceiverTest;

script DisabledReceiverTestScript drTest;
dr_test_driver TestDriver(execution_timeout: 2000) script;

DisabledReceiverTestScript MACHINE test {
	OPTION step 0;

    ok WHEN SELF IS ok;
    idle DEFAULT;
    working DURING run {
		INC step;
		SEND test_send_to_disabled TO test;
		WAITFOR test IS test_send_to_disabled_ok;

		INC step;
		SEND test_call_to_disabled TO test;
		WAITFOR test IS test_call_to_disabled_ok;

		INC step;
		SEND test_on_error_raises TO test;
		WAITFOR test IS test_on_error_raises_ok;

		INC step;
		SEND test_on_error_aborts TO test;
		WAITFOR test IS test_on_error_aborts_ok;

		INC step;
		SEND test_nested_raises TO test;
		WAITFOR test IS test_nested_raises_ok;

		SET test TO idle;
		SET SELF TO ok;
	}

	ENTER ok {
		LOG "" + step + " tests passed";
	}

}


AbortTest MACHINE {
	OPTION count 0;

	tick WHEN SELF IS idle AND TIMER >= 1000;
	idle DEFAULT;

ENTER tick {
	count := count + 1;
	LOG "This message before an IF should be displayed" + count + "/3";
	IF (count >= 3) { 
		LOG "This tick message before a THROW should be displayed";
		THROW overrun ;
		LOG "This tick-after-overrun message should not be displayed";
	};
	LOG "This message after an IF should only be displayed if the tick-after-overrun message has not been displayed"
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
	LOG "Overrun detected. Disabling SELF";
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

