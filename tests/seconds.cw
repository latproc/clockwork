# here is a machine that comes on briefly every second


# a fairly obvious idea but NOW tests do not generate wakeups so NOW cannot be used properly in a WHEN
EachSecond MACHINE {
	on WHEN NOW % 1000 = 0;
	off DEFAULT;
}

EverySecond {
	EVERY 1000 ...

/*
TimeKeeper {
	idle DEFAULT;

	COMMAND check {
		delay := NOW - NOW % 1000 - 10;
*/
