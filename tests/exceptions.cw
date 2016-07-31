# tests of exceptions
#

AbortTest MACHINE {
	OPTION count 0;

	tick WHEN SELF IS idle AND TIMER > 1000;
	idle DEFAULT;

ENTER tick {
	count := count + 1;
	IF (count > 10) { 
		THROW overrun ;
		LOG "This tick message should not be displayed";
	};

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

