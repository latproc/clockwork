# this configuration is for a client machine the 'real' device
# is connected to the client and is implemented as shown below.

FlasherMachine MACHINE {
	OPTION delay 100;
    on WHEN SELF IS on,  EXECUTE turnOff WHEN SELF IS on && TIMER >= delay;
    off WHEN SELF IS off, EXECUTE turnOn WHEN SELF IS off && TIMER >= delay;
  	stopped WHEN SELF IS stopped;
    off DURING turnOff { }
    on DURING turnOn { }

    COMMAND start { SET SELF TO off }
    COMMAND stop { SET SELF TO stopped }
}

#Note: the above definition can be made in a file that is shared by
#    both ends of the channel

flasher FlasherMachine;
flasher0 FlasherMachine;
flasher1 FlasherMachine;
flasher2 FlasherMachine;
flasher3 FlasherMachine;
flasher4 FlasherMachine;

/*
 the client machine instantiates a channel to the server
 machine using the properties defined here.  Both sides
 are expected to have a 'FlasherChannel' definition that
 they use for the communication.

 it is not required that both sides actually use the same
 definition for the channel unless one side requires it
 by the channel configuration IDENTIFIER.
*/

flasher_chn FlasherChannel(host:"127.0.0.1", port:5555);

/*
The host and port provided in the channel instantiation are not the
identifiers for the channel per se, rather they provide information
on how to connect to the server to get the channel started.

When the channel is setup on the server, it's name will be provided
to the client and the client will then subscribe to the channel.
*/

client_flag FLAG;

item1 Item(barcode:1234, serial:1111);
