/* Define a test driver that runs tests in the machine given by parameter 

    Note that the driver waits for a run command (eg from iosh: SEND driver.run)
    
    The test machine is expected to enter a working state while its run
    command executes and then to enter an error or ok state when the
    tests are completed.
    
    To reset the test script, it is disabled and reenabled; this process
    causes a machine to reinitialise.
    
    If the machine under test does not change state within 10 seconds (10000ms),
    it is assumed to have stalled and the driver regards this as an error.
*/
Driver MACHINE test {
	OPTION execution_timeout 10000;
    script TestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > execution_timeout;
    ok WHEN script IS ok ;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}

/*
 in your program instantiate a test script and pass it to 
 an instance of this diver like this:

driver Driver test_machine;

*/

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
        # perform tests here
        # - set the step property to the current step number
        # - setup the test conditions
        # - send a message to the device under test to start the test
        # - use WAITFOR to hang until the test is a success. eg: WAITFOR test IS done_ok;
        SET SELF TO ok;
    }
}