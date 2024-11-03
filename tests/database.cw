
# Generic sampling. Any machine may be monitored at the server
DATABASE_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 10708;
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	PUBLISHER;
	MONITORS `.*`;
	IGNORES STATE_CHANGES, PROPERTY_CHANGES;
}
