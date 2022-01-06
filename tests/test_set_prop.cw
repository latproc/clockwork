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
  busy DURING one {
    x := PROPERTY y;
    WAITFOR x == "y value";
    SET SELF TO ready;
  }
  busy DURING two {
    x := PROPERTY z OF lookup;
    WAITFOR x == lookup.key;
    SET SELF TO ready;
  }
  busy DURING three {
    SET x TO PROPERTY z OF unknown;
    SET SELF TO ready;
  }

  CATCH invalid_param { SET SELF TO error; }
}
test_set_prop_machine SetPropertyTest;

SetPropTestScript MACHINE test {
    OPTION step 0;

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
        INC step;
        CALL test.reset;
        CALL one ON test;
        WAITFOR test IS ready;
        INC step;
        CALL test.reset;
        CALL two ON test;
        WAITFOR test IS ready;
        INC step;
        CALL test.reset;
        # Expression failures do not currently throw errors (TODO)
        #SEND three TO test;
        #WAITFOR test IS error;
        LOG "PASSED";
        SET SELF TO ok;
    }
}

Test_SetPropDriver MACHINE test {
    script SetPropTestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}

test_set_prop_driver Test_SetPropDriver test_set_prop_machine;
