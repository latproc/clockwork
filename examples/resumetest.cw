/*
This program tests the behaviour of timers when machines are disabled
and then resumed.

Once the test machine starts (after receiving a start message) it turns a 
motor on and expects something to turn on a proximity. In our case
we do so manually in iosh.

For a normal operation:

	SEND test.start;
  .. after a few seconds.. 
  TOGGLE sim_prox;

For an interrupted operation (a door is opened on the machine)

	SEND test.start;
  .. after a few seconds.. 
	SEND test.open_door;
  .. some time later;
  SEND test.close_door;
  TOGGLE sim_prox;
  
in either case, the test will jump to an error state if 10 seconds 
elapses before the proximity (sim_prox) comes on but the timer is 
paused during the time the door is open.

At the end of the process, the State machine reports the time
taken for the work, ignoring the door-open time.

*/

State MACHINE {
	idle INITIAL;
  working STATE;
	LEAVE working { LOG "work took " + TIMER + "ms"; }
}

ResumeTimerTest MACHINE motor, prox {
OPTION Timeout 10000;
state State;

error WHEN SELF IS error;
idle WHEN state IS idle;

work_done WHEN SELF IS working AND prox IS on;
working WHEN SELF IS working AND motor IS on,
	EXECUTE timeout WHEN state.TIMER > Timeout;
working WHEN SELF IS work_start AND motor IS on;
work_start WHEN (SELF IS work OR SELF IS work_start) AND state IS working;
work WHEN state IS working;

ENTER work_start {SET motor TO on; }
ENTER work_done { SET motor TO off; SET state TO idle; }
error DURING timeout { SET motor TO off; }

COMMAND start { SET prox TO off; SET state TO working; }

COMMAND open_door { DISABLE state; }
COMMAND close_door { RESUME state; }

}

sim_prox FLAG;
sim_motor FLAG;
test ResumeTimerTest sim_motor, sim_prox;
