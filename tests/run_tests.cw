# Run a list of tests
#
# This is a work in progress, not currently runnable
#
# This file uses the SYSTEM machine to build a list of
# testable machines. It assumes that machines matching a
# certain interface are testable

TestDriver INTERFACE {
  ok STATE; # tests ran to successful completion
  error STATE; # tests failed to complete
  waiting STATE; # waiting for a task to complete
  idle STATE; # driver has nothing to do

  COMMAND run; # start running the tests
  COMMAND abort; # abort the tests
}

# run this with:
# cw run_tests.cw arith.cw bitset.cw anyon.cw test_set_prop.cw

all_tests LIST
  test_arith_driver
  , bt_driver
  , test_any_on1_driver
  , test_any_on2_driver
  , test_prop_driver
  , test_set_prop_driver
  ;

run_tests RunAllTests all_tests;

RunAllTests MACHINE all_tests {

	tests LIST;
	stage REFERENCE;

	active FLAG;

	inactive WHEN active IS off; # used in the stop command to abort execution of the test

  completed_successfully WHEN ALL all_tests ARE ok;

	complete WHEN tests IS empty AND stage IS NOT ASSIGNED;
	waiting WHEN tests IS nonempty AND stage IS NOT ASSIGNED;
	unknown WHEN stage.ITEM IS Unknown;

	ready WHEN stage IS ASSIGNED AND stage.ITEM IS idle;
	working WHEN stage IS ASSIGNED AND stage.ITEM IS waiting;
	done WHEN stage IS ASSIGNED AND (stage.ITEM IS ok || stage.ITEM IS error);
	idle DEFAULT;

	COMMAND stop {
		SEND stop TO stage;
		SET active TO off;
	}

	ENTER ready { SEND run TO stage; }
	ENTER done {
    LOG "completed " + stage.ITEM.NAME + ": " + stage.ITEM;
		SEND reset TO stage;
		CLEAR stage;
	}
	ENTER waiting {
		CLEAR stage;
		a := TAKE FIRST FROM tests;
		ASSIGN a TO stage;
	}
	setting_up DURING setup {
		SET active TO on;
		CLEAR tests;
		CLEAR stage;
    COPY ALL FROM all_tests TO tests;

    # in future this will be able to run using the list of loaded machines.

    # perhaps (simplest to get started) something like:
		#   COPY ALL FROM SYSTEM.TYPES TO tests WHERE SYSTEM.TYPES.ITEM.NAME MATCHES `Test_.*`;
    # or (better)
		#   COPY ALL FROM SYSTEM.TYPES TO tests WHERE SYSTEM.TYPES.ITEM IMPLEMENTS TestDriver;
	}
}
