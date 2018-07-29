# check that dependent machines are updated when a machine changes
# state or property value
# 


StateChanger MACHINE { 
	on WHEN SELF IS off;
	off DEFAULT;
}
fast_clock StateChanger;

Counter MACHINE clock {
	OPTION count 0;
	Ready INITIAL;
	COMMAND Reset { count := 0; }
	RECEIVE clock.on_enter { count := count + 1; }
}
counter Counter fast_clock;

DummyCounter MACHINE clock {
	OPTION count 0;
	Ready INITIAL;
	COMMAND Reset { count := 0; }
	RECEIVE clock.on_enter { count := 0; }
}
dummy DummyCounter fast_clock;

Monitor MACHINE counter{
	OPTION count 0;
	on WHEN counter.count % 10 == 0;
	off DEFAULT;
	ENTER on { count := (count + 1) % 10; }
}

units Monitor counter;
tens Monitor units;
hundreds Monitor tens;
units1 Monitor dummy;
units2 Monitor dummy;
units3 Monitor dummy;
units4 Monitor dummy;
units5 Monitor dummy;
units6 Monitor dummy;
units7 Monitor dummy;
units8 Monitor dummy;
units9 Monitor dummy;
units0 Monitor dummy;
