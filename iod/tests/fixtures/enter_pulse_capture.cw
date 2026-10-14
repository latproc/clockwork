# enter_pulse_capture.cw
#
# A one-eval pulse: a machine is put into Done and taken straight back out
# inside a single action sequence, so the state exists only between two SETs.
# That is the shape a short handshake has on a plant (`SET M_Control TO Done;`
# followed by the same pass leaving it), and it is why a listener that can only
# look at the level can miss it.
#
# Three listeners watch the same pulse, one per mechanism:
#
#   IS       level only. Evaluated when the listener is walked, which is after
#            the pulse has gone, so it never sees Done.
#   ENTERED  the enter is recorded against the listener when it happens and
#            survives until the listener's pass, so it sees Done.
#   RECEIVE  the enter message runs on the source's own stack, so it sees Done
#            with no pass in between at all.
#
# Markers, asserted by CMake with bounded runs (run_cw_runtime.sh takes one
# --must-contain per invocation):
#   enter_pulse_is_is_level_only   must-not-contain "PULSE-IS-SAW-DONE"
#   enter_pulse_entered_sees_pulse must-contain     "PULSE-ENTERED-SAW-DONE"
#   enter_pulse_receive_sees_pulse must-contain     "PULSE-RECEIVE-SAW-DONE"

PULSER MACHINE {
	OPTION want 0;
	Wait DEFAULT;
	Done WHEN want == 1;
	Idle WHEN want == 2;
}

IsWatch MACHINE {
	Wait DEFAULT;
	SawDone WHEN PULSED IS Done;
	ENTER SawDone {
		LOG "PULSE-IS-SAW-DONE";
	}
}

EnteredWatch MACHINE {
	GLOBAL PULSED;
	Wait DEFAULT;
	SawDone WHEN PULSED ENTERED Done;
	ENTER SawDone {
		LOG "PULSE-ENTERED-SAW-DONE";
	}
}

ReceiveWatch MACHINE {
	GLOBAL PULSED;
	Wait DEFAULT;
	RECEIVE PULSED.Done_enter {
		LOG "PULSE-RECEIVE-SAW-DONE";
	}
}

PulseDriver MACHINE {
	ticking WHEN TIMER >= 200;
	wait DEFAULT;
	ENTER ticking {
		SET PULSED TO Done;
		SET PULSED TO Idle;
		LOG "PULSE-SENT";
	}
}

PULSED PULSER;
iswatch IsWatch;
enteredwatch EnteredWatch;
receivewatch ReceiveWatch;
pulsedriver PulseDriver;
