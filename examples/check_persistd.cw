# this script checks whether persistd is running
#
# if it isn't running for 30 seconds, a warning messages is logged

CheckPersistd MACHINE {
	missing WHEN NOT (PERSISTENCE_CHANNEL EXISTS), EXECUTE warning WHEN TIMER>30000;
	ok DEFAULT;

	COMMAND warning { LOG "WARNING: persistd is not running" }
}
check_persistd CheckPersistd;
