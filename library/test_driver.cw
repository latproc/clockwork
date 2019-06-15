/* Defines a test driver that runs tests in the machine given by parameter 

    Note that the driver waits for a run command (eg from iosh: SEND driver.run)
    
    The script machine is expected to enter a working state while its run
    command executes and then to enter an error or ok state when the
    tests are completed.
    
    To reset the script, it is disabled and reenabled; this process
    causes it to reinitialise.
    
    If the machine under test does not change state within 10 seconds (10000ms),
    it is assumed to have stalled and the driver regards this as an error.
*/
TestDriver MACHINE script {
	OPTION execution_timeout 10000;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > execution_timeout;
    ok WHEN script IS ok ;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
    COMMAND reset { SET SELF TO idle }
}
