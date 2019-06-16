# test stable state transitions and their various additions

Simple MACHINE {
	inactive INITIAL; inactive WHEN SELF IS inactive;
	on WHEN SELF IS on AND TIMER < 1000 || SELF IS off AND TIMER >= 1000;
	off DEFAULT;
}
simple Simple;

Split MACHINE {
	inactive INITIAL; inactive WHEN SELF IS inactive;
	on WHEN SELF IS off AND TIMER >= 1000;
	off WHEN SELF IS on AND TIMER >= 1000;
	on WHEN SELF IS on;
	off WHEN SELF IS off;
}
split Split;

SplitTransitions MACHINE {
	inactive INITIAL; inactive WHEN SELF IS inactive;
	off WHEN SELF IS starting;
	on WHEN SELF IS off AND TIMER >= 1000;
	off WHEN SELF IS on AND TIMER >= 1000;
	on WHEN SELF IS on;
	off WHEN SELF IS off;

	starting DURING start { LOG "starting" }

	TRANSITION inactive TO off ON start;
}
trans SplitTransitions;

