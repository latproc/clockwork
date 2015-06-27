# This is a demonstration of a clockwork channel

# the flasher interface
# note that no INITIAL state is provided so, like MACHINEs, the
# FlagInterface has an implicit initial state of INIT.

FlagInterface INTERFACE {
	OPTION x , y;
	OPTION z 100;

    on STATE;
    off STATE;
    
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
}

# The FlasherChannel updates two machines that happen to both
# use the same interface but note that the machines themselves
# can be instantiated at either end of the channel
