/* provides examples that utilise the state changing mechanims 
*/

SlowSwitch MACHINE {
	LOCAL OPTION delay 20;
	on STATE; off INITIAL; ENTER on { WAIT delay; } ENTER off { WAIT delay; }
	TRANSITION on TO off USING toggle; TRANSITION off TO on USING toggle;
}

ChangeMonitor MACHINE target {
	active WHEN target CHANGING STATE;
	idle DEFAULT;
}

StateTest MACHINE {

	LOCAL OPTION delay 1000;
	switch SlowSwitch;
	mon ChangeMonitor switch;

	on STATE; off STATE;

	checking WHEN SELF IS waiting AND TIMER >= delay;
	waiting DEFAULT;
	ENTER checking {
		SEND toggle TO switch;
	}
}
test StateTest;
