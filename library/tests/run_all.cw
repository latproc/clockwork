# run all library tests

one_shot_timer OneShotTimer;
test_one_shot_timer TestOneShotTimer;

all_library_tests LIST test_one_shot_timer;

test_driver MultipleTestDriver all_library_tests;

