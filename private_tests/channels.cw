# This is a test for channel definitions
#

# Generic sampling. Any machine may be monitored at the server
SAMPLER_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 10600;
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	MONITORS `.*`;
}

# at the client:
SAMPLER SAMPLER_CHANNEL(host:"localhost");

# The standard definition for persistence
PERSISTENCE_CHANNEL CHANNEL {
	OPTION host "localhost";
	MONITORS PERSISTENT == "true";
}

# The standard modbus channel
MODBUS_CHANNEL CHANNEL {
    OPTION HOST "localhost";
    MONITORS MACHINES WITH PROPERTY export;
}

Flasher MACHINE {
    on WHEN SELF IS on || SELF IS off AND TIMER>1000,
        EXECUTE turnOff WHEN TIMER > 1000;
    off DEFAULT;
    off DURING turnOff { }
}
flasher Flasher;

SettingsTest MACHINE {
    OPTION PERSISTENT TRUE;
    OPTION dial_setting 54;
    ready INITIAL;
}
settings SettingsTest;

# If the client uses the above instantiation, all machines
# will be monitored and sent to the sample client. This
# has a large performance impact and thus it can be 
# reduced by listing the specific machines at the client
#
# in other words, messages are not generated unless a 
# channel actually needs them

LightMonitorChannel CHANNEL EXTENDS SampleChannel {
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	MONITORS light_switch, light;
}


ScalesChannel CHANNEL {
	OPTION host "localhost";
	OPTION port 10601;
	UPDATES scales;
}

ScalesInterface INTERFACE {
    OPTION weight, status;
    stable STATE;
    unstable DEFAULT;
}

Scales MACHINE {
	OPTION weight 0;
	OPTION status "";
	stable WHEN status == "S";
	unstable DEFAULT;

	ENTER stable { LOG weight }
}
scales Scales;

# on the remote instance of clockwork:

scales_channel ScalesChannel;

