# Guard used directly on a real output

GUARDOUTPUTSTATEFIX MACHINE Output {
#	onFix WHEN OWNER IS on && Output IS off;
#	offFix WHEN OWNER IS off && Output IS on;
	onFix WHEN Output IS off;
	offFix WHEN Output IS on;
	idle DEFAULT;

	ENTER onFix { SET Output TO on; }
	ENTER offFix { SET Output TO off; }
}

GUARD MACHINE (image:input64x64) Guard, O_Output {
	onFlag FLAG;

	Fix GUARDOUTPUTSTATEFIX O_Output;

	disabled WHEN O_Output DISABLED;
	interlocked WHEN Guard IS false;
	on WHEN onFlag IS on && Guard IS true;
	off DEFAULT;

	ENTER interlocked { SET O_Output TO off; }

	ENTER on { SET O_Output TO on; }

	ENTER off { SET O_Output TO off }

	COMMAND requeston { SET onFlag TO on; }

	COMMAND requestoff { SET onFlag TO off; }	

	TRANSITION ANY TO on USING requeston;
	TRANSITION ANY TO off USING requestoff;
	TRANSITION ANY TO interlocked USING noop;
	TRANSITION ANY TO disabled USING noop;
}


Point MACHINE { on STATE; off STATE; }
BasicGuard MACHINE { true STATE; false INITIAL; }
out1 Point;
ok BasicGuard;
guard GUARD ok, out1;

