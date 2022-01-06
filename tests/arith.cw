/** arith.cw

This script performs tests of various arithmetic operations
and demonstrates and experimental system for running test
cases.
*/

# This is a simple machine that checks whether basic boolean  comparisons
# are working.  The machine should sit in the 'ok' state.
Arithmetic MACHINE {

	bug WHEN SELF IS error OR ( 1==1 && 3>4);
	bad WHEN 12 == 10 AND ( 2 < 3 OR 4==5);
	ok WHEN 10 == 10 AND (2 < 3 OR 4== 5);
	error STATE;
	unknown DEFAULT;

	ENTER ok { LOG 1 + - 1; }
}
arith Arithmetic;

BadMultiply MACHINE {
	LOCAL OPTION x 3;
	COMMAND test {
		x := aa * 2;
		x := 2 * "2.01 ";
	}
}
failed_test BadMultiply;

/* The above machine is not generic and so does not help us run
 a series of test cases. Below we try to improve on that, first
 by defining a ComparisonTest machine that compares two values.
*/

ComparisonTest MACHINE a,b{
    eq WHEN a.VALUE == b.VALUE;
    lt WHEN a.VALUE < b.VALUE;
	gt WHEN a.VALUE > b.VALUE;
    error DEFAULT;
}

/* We instantiate the machine and two variables that are its inputs */
x VARIABLE 1;
y VARIABLE 2;
test_machine ComparisonTest x, y;

/* Now we define a test driver that runs tests in the machine given by parameter

    Note that the driver waits for a run command (eg from iosh: SEND driver.run)

    The test machine is expected to enter a working state while its run
    command executes and then to enter an error or ok state when the
    tests are completed.

    To reset the test script, it is disabled and reenabled; this process
    causes a machine to reinitialise.

    If the machine under test does not change state within 10 seconds (10000ms),
    it is assumed to have stalled and the driver regards this as an error.
*/
Test_ArithDriver MACHINE test {
	OPTION execution_timeout 10000;
    script TestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > execution_timeout;
    ok WHEN script IS ok ;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error at step: " + script.step; CALL abort ON SELF }
}
test_arith_driver Test_ArithDriver test_machine;

/* Here is the TestScript; during the working state, the machine
    changes the values of the inputs to the test and monitors
    the system output.

    The test we are using is the above ComparisonTest machine and
    this machine should enter the state of lt, gt or eq by comparing
    its inputs.  If the machine moves to an error state, the
    script stalls and the driver will soon notice this and raise
    the error.

    For convenience, the test machine keeps a step property as a
    trace variable to indicate to the driver which step has failed.
*/

TestScript MACHINE test {
	OPTION step 0;

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
		# integer tests
		step := 1;
		x.VALUE := 1; y.VALUE := 2;
		WAITFOR test IS lt;

		INC step;
		x.VALUE := 1;
		WAITFOR test IS lt;

		INC step;
		x.VALUE := 1; y.VALUE:=1;
		WAITFOR test IS eq;

		INC step;
		x.VALUE := 3; y.VALUE:=1;
		WAITFOR test IS gt;

		INC step;
		x.VALUE := 1 + 2 * 3; y.VALUE := 7;
		WAITFOR test IS eq;

		INC step;
		x.VALUE := 7-(2 - 1);
		WAITFOR test IS lt;

		INC step;
		y.VALUE := 6;
		WAITFOR test IS eq;

		INC step;
		TestVal := 7;
		x.VALUE := 7 - TestVal;
		WAITFOR test IS lt;

		INC step;
		y.VALUE := 0;
		WAITFOR test IS eq;

		INC step;
		Zone := 1;
		Location := 1;
		x.VALUE := (Zone + -1) % 9 - (Location - 1) * 3 + 1;
		WAITFOR test IS gt;

		INC step;
		y.VALUE := 1;
		WAITFOR test IS eq;

		# floating point tests
		INC step;
		x.VALUE := 1.1;
		y.VALUE := 1.0;
		WAITFOR test IS gt;

		INC step;
		y.VALUE := 1.1;
		WAITFOR test IS eq;


        SET SELF TO ok;
    }
}


