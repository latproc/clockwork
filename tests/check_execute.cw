# test whether executes happen correctly when multiple state clauses exist
# for the same state

Test MACHINE {

	LOCAL OPTION x 0, y 0;
	one WHEN x==1;
	one WHEN y == 1,
		EXECUTE logone WHEN TIMER >= 100,
		EXECUTE logtwo WHEN TIMER >= 200;
	idle DEFAULT;

	COMMAND logone { LOG "one"; }
	COMMAND logtwo { LOG "two"; }
}
test Test;
