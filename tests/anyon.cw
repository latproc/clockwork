# this script tests various combinations of ANY .. ARE.. situations.
# It drives the flags on and off and checks that the test machine
# performs the correct state changes.
# If the test machine fails to change state it is reset and the
# driver locks itself into an error state.
#
# functions tested:
#
#  use of ANY .. ARE  using a property for the state being tested
#  use of two ANY .. ARE clauses in the same rule
#  continued execution of scripts after a WAITFOR
#  resetting a machine from a script
#
# To run this test:
#   SEND driver.run;
#   MESSAGES;
# at the end, driver will be in the 'error' or 'ok' state
#
# can repeat this with driver2

anyon_f1 FLAG;
anyon_f2 FLAG;
anyon_l1 LIST anyon_f1,anyon_f2;
anyon_x1 FLAG;
anyon_x2 FLAG;
anyon_l2 LIST anyon_x1,anyon_x2;
list1 LIST;
list2 LIST;

AnyOnTest MACHINE a,b{
    both WHEN ANY a ARE on AND ANY b ARE on;
    on_a WHEN ANY a ARE on;
    on_b WHEN ANY b ARE on;
    off DEFAULT;
}
anyon_test_machine AnyOnTest list1, list2;

anyon_it1 FLAG;
anyon_it2 FLAG;
anyon_it3 FLAG;
anyon_l3 LIST anyon_it1, anyon_it2, anyon_it3;
IndirectTest MACHINE items {
	ENTER INIT { SET anyon_it2 TO on; state := on }
	ok  WHEN ANY items ARE state;
	not_ok DEFAULT;
}
anyon_indirect_test IndirectTest anyon_l3;

AnyonTestScript MACHINE test {

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
        CLEAR test.a; CLEAR test.b;
        INCLUDE anyon_f1 IN test.a; INCLUDE anyon_f2 IN test.a;
        INCLUDE anyon_x1 IN test.b; INCLUDE anyon_x2 IN test.b;
        LOG "ANY ARE on is off when all items are off";
        SET anyon_f1 TO off; SET anyon_f2 TO off; SET anyon_x1 TO off; SET anyon_x2 TO off;
        WAITFOR test IS off;
        LOG "ANY ARE on is on when one item is on";
        SET anyon_f1 TO on;
        WAITFOR test IS on_a;
        SET anyon_f1 TO off;
        WAITFOR test IS off;
        LOG "can test for two ANY on in the same rule";
        SET anyon_f1 TO on;
        SET anyon_x1 TO on;
        WAITFOR test IS both;
        SET anyon_x1 TO off;
        WAITFOR test IS on_a;
        LOG "ANY on test is reevaluated when the list is changed";
        INCLUDE anyon_f1 IN list2;
        WAITFOR test IS both;
        LOG "PASSED";
        SET SELF TO ok;
    }
}

AnyOnWithSubMachineScript MACHINE {
    test AnyOnTest l1,l2;

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
        CLEAR test.a; CLEAR test.b;
        INCLUDE anyon_f1 IN test.a; INCLUDE anyon_f2 IN test.a;
        INCLUDE anyon_x1 IN test.b; INCLUDE anyon_x2 IN test.b;
        LOG "A test for ANY on is off WHEN all are off";
        SET anyon_f1 TO off; SET anyon_f2 TO off; SET anyon_x1 TO off; SET anyon_x2 TO off;
        WAITFOR test IS off;
        LOG "WAITFOR works when used to test a sub-machine state";
        SET anyon_f1 TO on;
        WAITFOR test IS on_a;
        LOG "PASSED";
        SET SELF TO ok;
    }
}

Test_AnyOn1Driver MACHINE test, test2 {
    script AnyonTestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok AND test2 IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}

Test_AnyOn2Driver MACHINE {
    script AnyOnWithSubMachineScript;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}
test_any_on1_driver Test_AnyOn1Driver anyon_test_machine, anyon_indirect_test;
test_any_on2_driver Test_AnyOn2Driver;
