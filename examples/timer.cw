Test MACHINE { 
	on WHEN SELF IS off AND TIMER > 1000;
	off DEFAULT;

	LEAVE off { LOG "was in off for " + TIMER + "ms"; }
}
test Test;
