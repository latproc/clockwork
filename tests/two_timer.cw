# This checks whether timer tests against higher values are
# still scheduled properly in the presence of timer tests
# with smaller comparisons.
#
# This test machine should oscillate between one and three.

TwoTimerTest MACHINE {

    one WHEN SELF IS idle AND TIMER >= 100;
    two WHEN SELF IS one AND TIMER >= 10;
	three WHEN TIMER < 1000;
    idle DEFAULT;
    
}

test_two_timers TwoTimerTest;
