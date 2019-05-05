# this script tests that short circuit evaluation works correctly
# It drives the flags on and off and checks that the test machine
# performs the correct state changes.
# If the test machine fails to change state it is reset and the 
# driver locks itself into an error state.

Test1 MACHINE {  bad WHEN 1==2 && 2==3; ok DEFAULT; }
Test2 MACHINE {  bad WHEN 2==2 && 4==5; ok DEFAULT; }
Test3 MACHINE {  ok WHEN 3==3 && 6==6; bad DEFAULT; }
Test4 MACHINE {  ok WHEN 4==4 && ( 8<7 || 6==6); bad DEFAULT; }
Test5 MACHINE {  ok WHEN (8<7 || 6==6) && 5==5; bad DEFAULT; }
Test6 MACHINE {  ok WHEN 8<7 && 3==3 || 6==6; bad DEFAULT; }

test1 Test1;
test2 Test2;
test3 Test3;
test4 Test4;
test5 Test5;
test6 Test6;
tests LIST test1, test2, test3, test4, test5, test6;

TestShortCiruitEvaluation MACHINE {

    ok WHEN SELF IS ok || ALL tests ARE ok;
    error WHEN SELF IS error || ANY tests ARE bad;
    idle DEFAULT;
}

test_shortcircuit TestShortCiruitEvaluation;