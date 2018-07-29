# This is a demonstration of a clockwork channel

# the flasher interface
# note that no INITIAL state is provided so, like MACHINEs, the
# FlagInterface has an implicit initial state of INIT.

FlagInterface INTERFACE {
	OPTION x , y;
	OPTION z 100;

    on STATE;
    off INITIAL;
    
    COMMAND turnOn;
    COMMAND turnOff;
    COMMAND toggle;
}

/*
 here we define a channel that communicates between the two clockwork
 drivers. The channel refers to a machine that is may be instantiated at one
 side of the channel. Any side of the channel that does not instantiate 
 a machine with the given name will instantiate a machine using the 
 named interface.
*/

FlasherChannel CHANNEL {
	OPTION port 7720;
    UPDATES flasher FlagInterface;
    UPDATES flag FlagInterface;
    UPDATES item1 ItemInterface;
    UPDATES item2 ItemInterface;
	UPDATES server_flag CHANNELMONITORINTERFACE;
	UPDATES client_flag CHANNELMONITORINTERFACE;
}

# The FlasherChannel updates two machines that happen to both
# use the same interface but note that the machines themselves
# can be instantiated at either end of the channel


channels LIST PERSISTENCE_CHANNEL_7901;
ChannelCheck MACHINE connections {
  on WHEN ALL connections ARE ACTIVE;
  off DEFAULT;
}
connection_check ChannelCheck channels;

# These items are shared across a channel. Each side has its 
# own item and a shadow of the other one

Item MACHINE {
    OPTION barcode 0;
    OPTION serial 0;
}

ItemInterface INTERFACE {
    OPTION barcode 0;
    OPTION serial 0;
}


CHANNELMONITORINTERFACE INTERFACE{
    interlocked INITIAL;
    on STATE;
    off STATE;
}


SIMPLEFOLLOWERA MACHINE other {
    interlocked WHEN other DISABLED;
    error1 WHEN SELF IS NOT other.STATE AND TIMER>10 AND other.TIMER>10;
    on WHEN SELF IS on;
    off WHEN SELF IS off;
		on WHEN other IS on;
    off WHEN other IS off;
    error1 DEFAULT;
}

SIMPLEFOLLOWERB MACHINE other {
    interlocked WHEN other DISABLED;
    error2 WHEN SELF IS NOT other.STATE AND TIMER>10 AND other.TIMER>10;
    on WHEN SELF IS on OR other IS on;
    off WHEN SELF IS off OR other IS off;
    error2 DEFAULT;
}


