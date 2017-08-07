# This is a demonstration of a clockwork channel

# the flasher interface
# note that no INITIAL state is provided so, like MACHINEs, the
# FlagInterface has an implicit initial state of INIT.

FlagInterface INTERFACE {
#	OPTION x , y;
#	OPTION z 100;
    EXPORT STATES on,off;

    on STATE;
	off STATE;
	stopped STATE;
    INIT INITIAL;
    
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
	OPTION host "127.0.0.1";
	OPTION port 7720;
    UPDATES flasher FlagInterface;
    UPDATES flag FlagInterface;
	UPDATES client_flag FlagInterface;
    UPDATES item1 ItemInterface;
    UPDATES item2 ItemInterface;
}

# The FlasherChannel updates two machines that happen to both
# use the same interface but note that the machines themselves
# can be instantiated at either end of the channel


#channels LIST PERSISTENCE_CHANNEL_7901;
ChannelCheck MACHINE connections {
  on WHEN ALL connections ARE ACTIVE;
  off DEFAULT;
}
#connection_check ChannelCheck channels;

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

