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

Driver MACHINE {

test Test(tab:Tests);
	COMMAND go { CALL next ON test }
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

TransitionToAny MACHINE {

	off STATE;
	idle DEFAULT;
	COMMAND log { LOG "going idle"; }
	TRANSITION INIT TO off,idle USING log;

}
toany TransitionToAny;
