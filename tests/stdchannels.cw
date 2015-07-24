
# Generic sampling. Any machine may be monitored at the server
SAMPLER_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 10600;
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	MONITORS `.*`;
	IGNORES `^SYSTEM`;
	PUBLISHER;
}

# The standard definition for persistence
PERSISTENCE_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 7901;
	MONITORS PERSISTENT == "true";
	THROTTLE 50;
	PUBLISHER;
	IGNORES STATE_CHANGES;
}

# The standard modbus channel
MODBUS_CHANNEL CHANNEL {
    OPTION HOST "localhost";
	OPTION port 7902;
    MONITORS MACHINES WITH EXPORTS;
	THROTTLE 50;
	PUBLISHER;
	MODBUS;
	IGNORES STATE_CHANGES, PROPERTY_CHANGES;
}

