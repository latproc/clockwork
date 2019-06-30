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
  ENTER error { LOG "error: " + script.NAME + " tests failed at step " + script.step; CALL abort ON SELF }
  COMMAND reset { SET SELF TO idle }

  ENTER ok { LOG script.NAME + " tests complete. " + script.step + " tests passed" }
}

# similar to the TestDriver but takes a list of tests and runs them all
MultipleTestDriver MACHINE list {
	OPTION execution_timeout 10000;

  script REFERENCE;
  started FLAG;

  error WHEN SELF IS error || SELF IS waiting && TIMER > execution_timeout;
  idle WHEN started IS off;
  starting_test WHEN SELF IS idle && SIZE OF list > 0;
  idle WHEN script IS NOT ASSIGNED;
  waiting WHEN script.ITEM IS working;
  ok WHEN SELF IS idle && started IS on AND SIZE OF list == 0;
  idle WHEN 1==1;

  ENTER starting_test {
    x := TAKE FIRST FROM list;
    ASSIGN x TO script;
    SEND run TO script; 
    WAITFOR script.ITEM IS working;
  }

  COMMAND run { SET started TO on; }

  COMMAND abort WITHIN waiting { 
    SET started TO off;
  }

  ENTER error { 
    LOG "error: " + script.ITEM.NAME + " tests failed at step " + script.ITEM.step; 
    CALL abort ON SELF
  }

  ENTER ok {
    LOG "all tests complete";
    SET started TO off;
  }

  COMMAND test_passed WITHIN waiting {
    LOG script.ITEM.NAME + " tests complete. " + script.ITEM.step + " tests passed"
  }

  TRANSITION waiting TO idle USING test_passed;
}
