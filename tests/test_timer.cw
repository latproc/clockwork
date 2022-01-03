# this script tests timers when used in expressions
# If the test machine fails a test it is reset and the
# driver locks itself into an error state.
#
# functions tested:
#
# WHEN.. TIMER > x AND expression
# WHEN.. expression >= TIMER
#
# To run this test:
#   SEND driver.run;
#   MESSAGES;
# at the end, driver will be in the 'error' or 'ok' state
#
# can repeat this with driver2

RHSExpressionTimerTest MACHINE {
  OPTION x 0;

  ready STATE;
  error STATE;
  done STATE;

  error WHEN SELF IS ready AND TIMER > 50 || SELF IS error;
  done WHEN SELF IS ready AND TIMER > 30  + 1|| SELF IS done;

  COMMAND reset { SET SELF TO INIT; }
  COMMAND one { SET SELF TO ready; }
}

LHSExpressionTimerTest MACHINE {
  OPTION x 0;

  ready STATE;
  error STATE;
  done STATE;

  error WHEN SELF IS ready AND 50 <= TIMER || SELF IS error;
  done WHEN SELF IS ready AND 30  + 1 <= TIMER || SELF IS done;

  COMMAND reset { SET SELF TO INIT; }
  COMMAND one { SET SELF TO ready; }
}


TestScript MACHINE rhs_test, lhs_test {

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
        CALL rhs_test.reset;
        SEND one TO rhs_test;
        WAITFOR rhs_test IS ready;
        WAITFOR rhs_test IS done;
        CALL lhs_test.reset;
        SEND one TO lhs_test;
        WAITFOR lhs_test IS ready;
        WAITFOR lhs_test IS done;
        LOG "PASSED";
        SET SELF TO ok;
    }
}

Driver MACHINE test1, test2 {
    script TestScript test1, test2;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}

rhs_test RHSExpressionTimerTest;
lhs_test LHSExpressionTimerTest;
driver Driver rhs_test, lhs_test;
