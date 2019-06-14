# This machine totals the amount of time that
# an 'input' is on. For comparison purposes it
# also records the time the monitor is on since 
# it is following the input on state anyway
#
# run this along with stdchannels.cw to see the count increasing

INPUTONTIME MACHINE Input {
	OPTION onTime 0;
	OPTION myOnTime 0;
	OPTION onCount 0;
	on WHEN Input IS on;
	off DEFAULT;
	LEAVE on { 
		myOnTime := myOnTime + TIMER;   # my TIMER is not reset at the point where this LEAVE is executed
		onCount := onCount + 1; 
	}
	# the input is already off by the time our automatic state change happens 
	# so we have to use a calculation
	# 
	OPTION start_time 0;
	RECEIVE Input.on_enter { start_time := NOW; }
	RECEIVE Input.on_leave { 
		# the input may already be on so we initialise as best we can
		IF (start_time == 0) { start_time := NOW};
		onTime := onTime + NOW - start_time; 
	}
  COMMAND reset { onTime := 0; onCount := 0; myOnTime := 0; }
}

on_timer INPUTONTIME input;
input SIMULATEDINPUT;


# a machine to generate on/off pulses
SIMULATEDINPUT MACHINE {

  on STATE;
  off INITIAL;

  ENTER on { WAIT 1000; SEND turnOff TO SELF; }
  ENTER off { WAIT 1000; SEND turnOn TO SELF; }
  off DURING turnOff{}
  on DURING turnOn{}

}


