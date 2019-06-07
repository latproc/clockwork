/*
A device connector test

This configuration provides a counter that continuously increments
as long as a device in "running". The "running" status is proveded
by device connector listening for input from a TCP connection. If
two seconds pass with no input, the device connector sets status
to "disconnected" and otherwise sets it to "running".

To run device_connector, use:

 ./device_connector --host localhost --watch_property counter.count --client --property sampler.message --pattern '.*' --name sampler --no_timeout_disconnect

Using this configuration, apart from the status messages on the 'sampler' machine
getting updated, any data sent from the monitored device is copied to the 
'message' property in the 'sampler' machine.

*/


DeviceConnector CHANNEL {
  OPTION port 7711;
  MONITORS counter;
  PUBLISHER;
}

# This program monitors a device to detect an input.
# when the input comes a process is started.
# when the input goes off the process is stopped.

Counter MACHINE {
	OPTION VALUE 0;

	running WHEN SELF IS NOT idle, EXECUTE count WHEN TIMER > 1000;
	idle INITIAL;

	counting DURING count { VALUE := VALUE + 1; }
	COMMAND reset { VALUE := 0; SET SELF TO idle; }

	TRANSITION idle TO running ON start;
	TRANSITION ANY TO idle ON stop;
}


# The status property in this machine will be updated by the device connector
# whenever it loses connection to its device.

Device MACHINE process {
	OPTION status "unknown";
	on WHEN status == "running";
	off DEFAULT;
	ENTER on {  SEND start TO process; }
	ENTER off { SEND stop TO process; }
}

counter Counter;
sampler Device counter;

