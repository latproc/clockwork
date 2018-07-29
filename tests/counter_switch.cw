# A counter based input switch

CounterSwitchNormallyOff MACHINE counter, input {
	LOCAL OPTION counter_start 0;
	OPTION distance 1000;
	on WHEN input IS on AND counter.VALUE - counter_start > distance;
	off DEFAULT;

	RECEIVE input.on_enter { counter_start := counter.VALUE; }
}

switch CounterSwitchNormallyOff sim_count, in;
sim_count CounterSimulator;
in FLAG;

CounterSimulator MACHINE {
	OPTION VALUE 0;
	count WHEN SELF == idle;
	idle DEFAULT;
	ENTER count { INC VALUE; }
}
