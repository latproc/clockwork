# saving the timer from a machine into a property

SaveTimer MACHINE other {
	LOCAL OPTION timer 0;
	COMMAND save { timer := other.TIMER; }
}

x FLAG(y:123);
saver SaveTimer x;
