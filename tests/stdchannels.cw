
# Generic sampling. Any machine may be monitored at the server
SAMPLER_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 10600;
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	MONITORS `.*`;
}

# The standard definition for persistence
PERSISTENCE_CHANNEL CHANNEL {
	OPTION host "localhost";
	MONITORS PERSISTENT == "true";
}
