# this script tests various combinations of bitset comparisons
# It drives the flags on and off and checks that the test machine
# performs the correct state changes.
# If the test machine fails to change state it is reset and the 
# driver locks itself into an error state.

f1 FLAG;
f2 FLAG;
l1 LIST f1,f2;
x1 FLAG;
x2 FLAG;
l2 LIST x1,x2;

BitsetTest MACHINE a,b{
    both WHEN BITSET FROM a == 3 && BITSET FROM a == BITSET FROM b;
    one WHEN (BITSET FROM a == 1 || BITSET FROM a == 2) && BITSET FROM a == BITSET FROM b;
    none WHEN BITSET FROM a == 0 && BITSET FROM a == BITSET FROM b;
    different WHEN BITSET FROM a != BITSET FROM b;
}
bitset_test BitsetTest l1,l2;

BTTestScript MACHINE test {

    ok WHEN SELF IS ok;
    idle DEFAULT;
    working DURING run {
        SET f1 TO off; SET f2 TO off; SET x1 TO off; SET x2 TO off;
        WAITFOR test IS none;
        SET f1 TO on;
        WAITFOR test IS different;
        SET x1 TO on;
        WAITFOR test IS one;
        SET x2 TO on;
        WAITFOR test IS different;
        SET f2 TO on;
        WAITFOR test IS both;
        SET f1 TO off; SET f2 TO off; SET x1 TO off; SET x2 TO off;
        WAITFOR test IS none;
        SET SELF TO ok;
    }
}

BTDriver MACHINE test {
    script BTTestScript test;
    ok WHEN script IS ok;
    error WHEN  SELF IS error || SELF IS waiting && TIMER > 10000;
    waiting WHEN script IS working;
    idle DEFAULT;

    COMMAND run { SEND run TO script; }
    COMMAND abort { DISABLE script; ENABLE script; }
    ENTER error { LOG "error"; CALL abort ON SELF }
}
bt_driver BTDriver bitset_test;
