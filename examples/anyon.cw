# this script tests various combinations of ANY .. ARE.. situations.
# It drives the flags on and off and checks that the test machine
# performs the correct state changes.
# If the test machine fails to change state it is reset and the 
# driver locks itself into an error state.
#
# functions tested:
#
#  use of ANY .. ARE  using a property for the state being tested
#  continued execution of scripts after a WAITFOR
#  resetting a machine from a script

f1 FLAG;
f2 FLAG;
l1 LIST f1,f2;
x1 FLAG;
x2 FLAG;
l2 LIST x1,x2;

AnyOnTest MACHINE a,b{
    both WHEN ANY a ARE on AND ANY b ARE on;
    on_a WHEN ANY a ARE on;
    on_b WHEN ANY b ARE on;
    off DEFAULT;
}
test_machine AnyOnTest l1,l2;

it1 FLAG;
it2 FLAG;
it3 FLAG;
l3 LIST it1, it2, it3;
IndirectTest MACHINE items {
	ENTER INIT { SET it2 TO on; state := on }
	ok  WHEN ANY items ARE state;
	not_ok DEFAULT;
}
indirect_test IndirectTest l3;

TestScript MACHINE test {

    ok WHEN SELF IS ok || SELF IS working;
    idle DEFAULT;
    working DURING run {
        SET f1 TO off; SET f2 TO off; SET x1 TO off; SET x2 TO off;
        WAITFOR test IS off;
        SET f1 TO on;
        WAITFOR test IS on_a;
        SET f1 TO off;
        WAITFOR test IS off;
        SET f1 TO on;
        SET x1 TO on;
        WAITFOR test IS both;
        SET x1 TO off;
        WAITFOR test IS on_a;
        INCLUDE f1 IN l2;
        WAITFOR test IS both;
        SET SELF TO ok;
    }
}

Driver MACHINE test, test2 {
    script TestScript test;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    ok WHEN script IS ok AND test2 IS ok;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; WAITFOR script IS working }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}
driver Driver test_machine, indirect_test;
