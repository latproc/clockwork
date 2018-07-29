extender FLAG(tab:tests, type:pushbutton);
push SINGLEPISTON(tab:tests, stepTimer:100) extender;


SINGLEPISTON MACHINE C_Extend {
	OPTION PERSISTENT true;
	OPTION position 0;
    OPTION maxpos 80;
    OPTION type "piston";

	ExtendStep WHEN SELF IS Extending && TIMER >= stepTimer && position < maxCount;
	Extending WHEN C_Extend IS on;

	RetractStep WHEN SELF IS Retracting && TIMER >= stepTimer && position > 0;
	Retracting DEFAULT;

	ENTER ExtendStep {
		IF (position < maxCount) {
			position := position + 1;
		};
	}

	ENTER RetractStep {
		IF (position > 0) {
			position := position - 1;
		};
	}
}

Cycle MACHINE {
    led FLAG(tab:tests);
    a WHEN SELF IS INIT || SELF IS b AND TIMER > 1000;
    b WHEN SELF IS a AND TIMER > 1000;

    ENTER a { SET led TO on; msg := "<pre>\n" + " Test\n</pre>"; LOG msg;}
    ENTER b { SET led TO off; }
}
cycler Cycle(tab:tests);
