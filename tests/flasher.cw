Pulse MACHINE { 
	on WHEN SELF IS off AND TIMER>=1000;
	off DEFAULT;
}
flasher0 Pulse;

FlasherOne MACHINE { 
	inactive INITIAL; inactive WHEN SELF IS inactive;
	on WHEN SELF IS on AND TIMER < 1000 OR SELF IS off AND TIMER>=1000;
	off DEFAULT;
}
flasher1 FlasherOne;

# this machine tests a cycle of events that use an execute and a transition table
# to drive the state machine.

# a two state machine. This machine needs to be kick started by being set to on or off

Flasher MACHINE {
	on WHEN SELF IS on, EXECUTE flip WHEN SELF IS on && TIMER>=1000;
	off WHEN SELF IS off, EXECUTE flip WHEN SELF IS off && TIMER>=1000;
	stopped STATE;
	COMMAND flip { SEND next TO SELF }
	COMMAND flipOn { SET SELF TO on;}
	COMMAND flipOff { SET SELF TO off; }
	TRANSITION on TO off ON next;
	TRANSITION off TO on ON next;
}
flasher Flasher (tab:tests);

# a three state machine. This machine needs to be kick started by being set to on or off

Cycle MACHINE {
	s1 WHEN SELF IS s1, EXECUTE flip WHEN TIMER>=1000;
	s2 WHEN SELF IS s2, EXECUTE flip WHEN TIMER>=1000;
	s3 WHEN SELF IS s3, EXECUTE flip WHEN TIMER>=1000;
	COMMAND flip { SEND next TO SELF }
	TRANSITION s1 TO s2 ON next;
	TRANSITION s2 TO s3 ON next;
	TRANSITION s3 TO s1 ON next;
}
cycle Cycle (tab:tests);
