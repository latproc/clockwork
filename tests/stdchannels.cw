
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
# Generic sampling. Any machine may be monitored at the server
SAMPLE_PROPERTIES_CHANNEL CHANNEL {
		OPTION host "localhost";
		OPTION port 10709;
		KEY "a0693d2bf6f463bf96f9d660b8e157fb";
		VERSION "0.1.0";
		MONITORS `.*`;
		IGNORES STATE_CHANGES;
		IGNORES `SYSTEM`;
		PUBLISHER;
}
#

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


# The standard display panel channel
PANEL_CHANNEL CHANNEL {
    OPTION HOST "localhost";
	OPTION port 7903;
    MONITORS MACHINES WITH EXPORTS;
	#THROTTLE 50;
	PUBLISHER;
	MODBUS;
	IGNORES STATE_CHANGES, PROPERTY_CHANGES;
}

