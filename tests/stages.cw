# This shows how to drive a list of processing stages

Stage MACHINE Processor {
	Unknown INITIAL;
	Ready STATE;
	Working STATE;
	Done STATE;
	Resetting STATE;
	Error STATE;

	COMMAND start { SEND start TO Processor; }
	COMMAND reset { SEND reset TO Processor; }
	COMMAND stop { SEND stop TO Processor; }
}


# minimal process
ProcessHelloWorld MACHINE stage {

	COMMAND start { SET stage TO Working; LOG stage.NAME + " Hello World"; SET stage TO Done; }
	COMMAND reset { SET stage TO Resetting; }
	COMMAND stop { SET stage TO Ready; }

	idle DEFAULT;
	ENTER idle { SET stage TO Ready; }
}

# a machine that counts to ten
ProcessCounter MACHINE stage {
	LOCAL OPTION x 0;

	active FLAG;

	inactive WHEN active IS off;
	done WHEN x >= 10;
	counting WHEN x < 10;
	idle DEFAULT;

	calculating DURING count { INC x; LOG x; }

	ENTER done { SET stage TO Done; }
	ENTER counting { SEND count TO SELF; }
	ENTER inactive { SET stage TO Ready; }

	COMMAND start { SET stage TO Working; SET active TO on; }
	COMMAND reset { SET stage TO Resetting; x := 0; SET active TO off; }
	COMMAND stop { SET active TO off; }
}

processOne ProcessHelloWorld stageOne;
processTwo ProcessCounter  stageTwo;
processThree ProcessHelloWorld stageThree;

stageOne Stage processOne;
stageTwo Stage processTwo;
stageThree Stage processThree;

all_stages LIST stageOne, stageTwo, stageThree;

test_stages_driver Test_StagesDriver all_stages;

Test_StagesDriver MACHINE stages {

	program LIST;
	stage REFERENCE;

	active FLAG;

	inactive WHEN active IS off; # used in the stop command to abort execution of the program

	complete WHEN program IS empty AND stage IS NOT ASSIGNED;
	waiting WHEN program IS nonempty AND stage IS NOT ASSIGNED;
	unknown WHEN stage.ITEM IS Unknown;

	ready WHEN stage IS ASSIGNED AND stage.ITEM IS Ready;
	working WHEN stage IS ASSIGNED AND stage.ITEM IS Working;
	done WHEN stage IS ASSIGNED AND stage.ITEM IS Done;
	resetting WHEN stage IS ASSIGNED AND stage.ITEM IS Resetting;
	idle DEFAULT;

	COMMAND stop {
		SEND stop TO stage;
		SET active TO off;
	}

	ENTER ready { SEND start TO stage; }
	ENTER done {
		SEND reset TO stage;
		CLEAR stage;
	}
	ENTER waiting {
		CLEAR stage;
		a := TAKE FIRST FROM program;
		ASSIGN a TO stage;
	}
	setting_up DURING setup {
		SET active TO on;
		CLEAR program;
		CLEAR stage;
		COPY ALL FROM stages TO program;
	}
}
