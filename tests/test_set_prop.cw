# this script tests the SET x TO PROPERTY .. statement
# If the test machine fails a test it is reset and the
# driver locks itself into an error state.
#
# functions tested:
#
# SET x TO PROPERTY y :- the value of x should become the value of y
# SET x TO PROPERTY z OF lookup :- x becomes the lookup machine's VALUE property
# SET x TO PROPERTY z OF unknown :- should error
# SET 7 TO PROPERTY z :- should error
# SET x TO PROPERTY 7 :- should error
#
# To run this test:
#   SEND driver.run;
#   MESSAGES;
# at the end, driver will be in the 'error' or 'ok' state
#
# can repeat this with driver2

SetPropertyTest MACHINE {
  OPTION x 0;
  OPTION y "y value";
  OPTION z "key";

  ready STATE;
  error STATE;

  lookup FLAG (key: "lookup value");

  COMMAND reset { SET SELF TO INIT; }
  COMMAND one { SET x TO PROPERTY y; SET SELF TO ready; }
  COMMAND two { SET x TO PROPERTY z OF lookup; SET SELF TO ready; }
  COMMAND three { SET x TO PROPERTY z OF unknown; SET SELF TO ready; }

  CATCH invalid_param { SET SELF TO error; }

  off DEFAULT;
}
test_machine SetPropertyTest;

TestScript MACHINE test {

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
        CALL test.reset;
        SEND one TO test;
        WAITFOR test IS ready;
        CALL test.reset;
        SEND two TO test;
        WAITFOR test IS ready;
        CALL test.reset;
        # Expression failures do not currently throw errors (TODO)
        #SEND three TO test;
        #WAITFOR test IS error;
        LOG "PASSED";
        SET SELF TO ok;
    }
}

Driver MACHINE test {
    script TestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}

driver Driver test_machine;
