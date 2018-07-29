# a Scheduler stress test


Stress MACHINE {
	OPTION VALUE 0;
	OPTION delay 100;

	idle WHEN SELF IS one AND TIMER> delay,
		EXECUTE count WHEN TIMER >= 1;
	one WHEN SELF IS one OR SELF IS two AND TIMER >delay,
		EXECUTE count WHEN TIMER >= 1;
	two WHEN SELF IS two OR SELF IS three AND TIMER>delay,
		EXECUTE count WHEN TIMER >= 1;
	three WHEN SELF IS three OR SELF IS four AND TIMER>delay,
		EXECUTE count WHEN TIMER >= 1;
	four WHEN SELF IS four OR TIMER > delay,
		EXECUTE count WHEN TIMER >= 1;
	idle DEFAULT;

	COMMAND count { INC VALUE; }
}

StressPair MACHINE other {
	OPTION VALUE 0;
	OPTION delay 100;
	OPTION exe 50;
	OPTION delta 0;
	OPTION last 0;

	init INITIAL; ENTER init { last := NOW; SET SELF TO idle; }

	idle WHEN SELF IS one OR SELF IS idle AND other.TIMER> delay,
		EXECUTE count WHEN TIMER >= exe;
	one WHEN SELF IS two OR SELF IS one AND  other.TIMER >delay,
		EXECUTE count WHEN TIMER >= exe;
	two WHEN SELF IS three OR SELF IS two AND  other.TIMER>delay,
		EXECUTE count WHEN TIMER >= exe; 
	three WHEN SELF IS four OR SELF IS three AND other.TIMER>delay,
		EXECUTE count WHEN TIMER >= exe;
	four WHEN SELF IS idle OR SELF IS four AND other.TIMER > delay,
		EXECUTE count WHEN TIMER >= exe;

	COMMAND count { INC VALUE; }
	LEAVE idle { delta := NOW - last; last := NOW; }

	LEAVE one { delta := NOW - last; last := NOW; }
	LEAVE two { delta := NOW - last; last := NOW; }
	LEAVE three { delta := NOW - last; last := NOW; }
	LEAVE four{ delta := NOW - last; last := NOW; }
}

stress_pair1 StressPair stress_pair2;
stress_pair2 StressPair stress_pair1;
