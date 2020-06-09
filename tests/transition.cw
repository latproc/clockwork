# this startup pattern is used to perform initialisation 
# in a way that reduces start race conditions

Startup MACHINE {
	idle DEFAULT;

	COMMAND startup WITHIN INIT { LOG "executing startup"; }

	TRANSITION INIT TO idle ON startup;
}
startup_test Startup;

Test MACHINE {

	one WHEN a == 1;
	two WHEN a == 2;

	s1 STATE;
	s2 STATE;

	ENTER one { LOG "one" }
	ENTER two { LOG "two" }
	ENTER s1 { LOG "s1" }
	ENTER s2 { LOG "s2" }

	COMMAND next { LOG "next"; }
	TRANSITION one TO s1 ON next;
	TRANSITION two TO s2 ON next;

	OPTION a 2;
}

SendNextAfterDelay MACHINE other {
  OPTION delay 50;
  enabled FLAG;
  idle WHEN enabled IS off;
  sending DEFAULT;
  ENTER sending {
    WAIT delay;
    SEND next TO other;
    SET enabled TO off;
  }
}

Driver MACHINE {
  test Test(tab:Tests);
  sender SendNextAfterDelay test; # used to send a message to test
  OPTION test_name "";

	COMMAND go { CALL next ON test }
  COMMAND go_s2 {
    test_name := "can WAITFOR a machine that auto transitions";
    test.a := 1; # let test auto transition to 'one'
    WAITFOR test IS one;
    test.a := 2; # let test auto transition to 'two'
    WAITFOR test IS two;
    LOG test_name + ": passed";

    test_name := "can WAITFOR a machine to change state using a transition";
    test.a := 3; # prevent auto transition of test when a is 1 or 2
    SEND next TO test;
    WAITFOR test IS s2;
    LOG test_name + ": passed";

    test_name := "can WAITFOR a machine to change via a transition during the waitfor";
    test.a := 2; # let test auto transition to 'two'
    WAITFOR test IS two;
    test.a := 3; # prevent auto transition of test when a is 1 or 2
    SET sender.enabled TO on;
    WAITFOR test IS s2;
    LOG test_name + ": passed";

    LOG "PASS";
  }
}
driver Driver (tab:Tests);

AutoTransition MACHINE {

	off INITIAL;
	on STATE;

	COMMAND stop { SET SELF TO off; }

	COMMAND turnOff { LOG "turning off" }
	COMMAND turnOn { LOG "turning on" }

	TRANSITION on TO off ON turnOff;
	TRANSITION off TO on ON turnOn;
}
auto AutoTransition;

TransitionFromAny MACHINE {
	off STATE;
	idle STATE;
	COMMAND log { LOG "going idle"; }
	TRANSITION ANY TO idle USING log;

}
toany TransitionFromAny;
