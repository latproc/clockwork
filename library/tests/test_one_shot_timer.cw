# to run the tests, send run to the test_driver.
#test_driver TestDriver call_tests_script; # see library/test_driver.cw

TestOneShotTimer MACHINE {
  GLOBAL one_shot_timer;
  OPTION step 0;

  ok WHEN SELF IS ok;
  idle DEFAULT;
  working DURING run {
    step := 1;

    # verify the timer triggers (not checking the time taken)
    one_shot_timer.timeout := 500;
    SEND start TO one_shot_timer;
    WAITFOR one_shot_timer IS expired;

    INC step;
    # verify the timer can be reset while running
    one_shot_timer.timeout := 100;
    SEND start TO one_shot_timer;
    LOG "waiting for one_shot_timer to start";
    WAITFOR one_shot_timer IS running;
    LOG "sending reset to one_shot_timer";
    SEND reset TO one_shot_timer;
    LOG "reset sent to one_shot_timer";

    WAITFOR one_shot_timer IS stopped;

    # add other tests here
    SET SELF TO ok;
  }
}

