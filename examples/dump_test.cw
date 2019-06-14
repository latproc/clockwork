ArmGuard MACHINE { true DEFAULT; }

UpAction MACHINE output {

    OPTION active_time 0;
    OPTION time_to_go 1000;
    
    blocked WHEN moving AND TIMER >= time_to_go;
    moving WHEN output IS on AND time_to_go > 0;
    
    ENTER blocked { active_time := active_time + time_to_go; }

}


Piston MACHINE extend_sol, retract_sol {

    OPTION position 0;
    OPTION direction 0;
    OPTION step_time 400;
    
    stopped WHEN direction == 0;
    extending WHEN TIMER < step_time AND position < 10 AND direction > 0;
    retracting WHEN TIMER < step_time AND position > 0 AND direction < 0;
    stepped_out WHEN position < 10 AND direction > 0;
    stepped_in  WHEN position > 0 AND direction < 0;
    extended WHEN position >= 10;
    retracted WHEN position <= 0;

    ENTER stepped_out { position := position + direction }
    ENTER stepped_in  { position := position + direction }
    
    # monitor the solenoids
    RECEIVE extend_sol.on_enter   { direction := 1 }
    RECEIVE extend_sol.off_enter  { direction := 0 }
    RECEIVE retract_sol.on_enter  { direction := 0-1 }
    RECEIVE retract_sol.off_enter { direction := 0 }

    COMMAND extend { SET retract_sol TO off; SET extend_sol TO on; }
    COMMAND retract { SET extend_sol TO off; SET retract_sol TO on; }
    COMMAND off { SET extend_sol TO off; SET retract_sol TO off; }
    
    # add the following to add an auto-off feature
    #ENTER extended  { SET extend_sol TO off }
    #ENTER retracted { SET retract_sol TO off }
}

ExtendedProx MACHINE piston {
    on WHEN piston.position >= 10;
    off DEFAULT;
}

RetractedProx MACHINE piston {
    on WHEN piston.position <= 0;
    off DEFAULT;
}



G_arm ArmGuard (tab:X);
timers DUMPACTIONTIMERS (tab:X);
O_dump_full FLAG (tab:X);
O_dump_part FLAG (tab:X);
O_arm1_up FLAG (tab:X);
O_arm1_down FLAG (tab:X);

C_dump DUMP (tab:X) O_dump_full, O_dump_part;
C_arm1_up DUMPACTION  (tab:X)timers, G_arm, O_arm1_up, C_dump;
C_arm1_down DUMPACTION  (tab:X)timers, G_arm, O_arm1_down, C_dump;

M_piston1 Piston (tab:X) C_arm1_up, C_arm1_down;
I_piston1_retracted RetractedProx (tab:X) M_piston1;
I_piston1_extended ExtendedProx (tab:X) M_piston1;




#M_arm1 Arm (tab:X) C_arm1_up, C_arm_down, OM_arm_up, OM_arm_down;

MonitoredOutput MACHINE output {
    on STATE;
    off STATE;
    OPTION timer 0;
    OPTION total_on 0;

    COMMAND turnOn { SET output TO on; SET SELF TO on; timer := 0; }
    COMMAND turnOff { 
        SET output TO off; 
        timer := TIMER;
        total_on := total_on + timer;
        SET SELF TO off;
    }
    COMMAND reset { total_on := 0; timer := 0; }
    
    TRANSITION off TO on USING turnOn;
    TRANSITION on TO off USING turnOff;
}

Metric MACHINE {
     idle INITIAL;
     running STATE;
     done STATE;
     
     COMMAND stop { timer := TIMER }
     COMMAND start { timer := 0 }
     
     TRANSITION idle TO running ON start;
     TRANSITION running TO done ON stop;
     TRANSITION done TO idle ON reset;
}

Arm MACHINE up_action, down_action, mon_up, mon_down {

   goingUp WHEN requestedUp IS on, EXECUTE amUp WHEN TIMER > 1000;
   up STATE;
   goingDown WHEN requestedDown IS on, EXECUTE amDown WHEN TIMER > 1000;
   down INITIAL;

   requestedUp FLAG (tab:X);
   requestedDown FLAG (tab:X);
   
   startingUp DURING raise {
        SET requestedUp TO on;
        SET up_action TO on;
   }
   
   ENTER up { SET up_action TO off; }
   
   startingDown DURING lower {
        SET requestedDown TO on;
   }
   
   ENTER down { SET down_action TO off; }
   
   TRANSITION up TO goingDown USING lower;
   TRANSITION down TO goingUp USING raise;

    COMMAND amDown { 
        SET requestedDown TO off;
        SET SELF TO down;
    }

    COMMAND amUp { 
        SET requestedUp TO off;
        SET SELF TO up;
    }
}

DUMPACTIONSTATUS MACHINE{
	interlocked_locked STATE; # interlocked and locked in the off state
	interlocked STATE; 		  # interlocked in the off state
	locked STATE;			  # locked in the off state
	on STATE;
	onpart STATE;
	off INITIAL;
}

DUMPACTIONTIMERS MACHINE {
	OPTION PERSISTENT true;
	OPTION ondelay 5;
	OPTION onpartdelay 5;
	OPTION offdelay 5;
	EXPORT RW 16BIT ondelay, onpartdelay, offdelay;
}

DUMPACTION MACHINE timers, G_guard, O_action, C_dump {
# locks are used whenever the status of C_dump is to be changed.
# this protocol helps ensure that no other dump action will touch
# the dump while this machine owns it.
	interlocked WHEN G_guard IS false || G_guard IS off;
	clearingInterlock WHEN (status IS interlocked || status IS interlocked_locked) 
		&& (G_guard IS true || G_guard IS on);
	locked WHEN C_dump != off && (SELF IS off || SELF IS locked);
	clearingLock WHEN (status IS locked) && C_dump == off;
	on STATE;
	onpart STATE;
	off INITIAL;

	## note: always set status before changing C_dump as the 
	##       messages from C_dump use the status value
	status DUMPACTIONSTATUS(tab:Outputs);

	ENTER clearingInterlock {
		CALL clearInterlock ON SELF;
	}

	ENTER clearingLock {
		CALL clearLock ON SELF;
	}

	# when our action goes off, we do not touch the dump, that happens
	# only through the turnoff command 
	ENTER off {
#		LOG "ENTER off";
	}
    
	ENTER on { 
#		LOG "ENTER on";
	}

	ENTER onpart { 
#		LOG "ENTER onpart";
	}

	COMMAND turnOn {
		IF (SELF != off) {
			LOG "Error: asked to turnOn when not off";
		}
		ELSE {
#			LOG "turnOn";
			SET status TO on;
			WAITFOR C_dump IS off;
			LOCK C_dump;
			SET O_action TO on; 
			WAIT timers.ondelay;
			SET C_dump TO on; 
		}
	}

	COMMAND turnOnPart {
		IF (SELF != off) {
			LOG "Error: asked to turnOnPart when not off";
		}
		ELSE {
#			LOG "turnOnPart";
			SET status TO onpart;
			WAITFOR C_dump IS off;
			LOCK C_dump;
			SET O_action TO on; 
			WAIT timers.onpartdelay;
			SET C_dump TO onpart; 
		}
	}


	COMMAND SlowToOnPart {
#		LOG "SlotToOnPart";
		IF (status != on) {
			LOG "error: slowing to onpart when not on";
		}
		ELSE {
			SET status TO onpart;
	#		WAITFOR C_dump IS on;
			SET C_dump TO onpart;
			SET status TO onpart;
		};
	}

	COMMAND turnOffAction {
			C_dump.offdelay := timers.offdelay;
			SET C_dump TO off;
			SET O_action TO off;
			UNLOCK C_dump;
	}

	COMMAND turnOff {
#		LOG "turnOff";
		IF (status != on && status != onpart) {
#			LOG "error: turnOff command received when not on or onpart";
		}
		ELSE {
			CALL turnOffAction ON SELF;
			SET status TO off;
		};
	}
    
	ENTER locked {
#		LOG "locked";
		IF (status IS off) {
			SET status TO locked;
		};
		IF (status IS on || status IS onpart) {
			LOG "error: entering locked state from on or onpart";
		};
	}
    
	ENTER interlocked { 
#		LOG "interlocked";
		IF (status IS on) {
			CALL turnOffAction ON SELF;
		};
		IF (status IS onpart) {
			CALL turnOffAction ON SELF;
		};
		IF (status IS locked) {
			SET status TO interlocked_locked
		};
	}

	RECEIVE G_guard.false_enter {
		SET SELF TO interlocked;
	}

	# when the guard clears, we return to our previous state
	COMMAND clearInterlock {
#		LOG "interlock cleared";
		IF ( (G_guard IS true|| G_guard IS on) &&  SELF == interlocked) { 
			IF (status IS on) {
				SET SELF TO off; # to ensure the turnOn action executes
				SET SELF TO on;
			};
			IF (status IS onpart) {
				SET SELF TO off; # to ensure the turnOn action executes
				SET SELF TO onpart;
			};
			IF (status IS interlocked_locked) {
				SET status TO locked;
				SET SELF TO locked;
			};
			IF (status IS off || status IS interlocked) {
				SET status TO off;
				SET SELF TO off;
			};
		}
	}

	RECEIVE G_guard.true_enter {
#		LOG "RECEIVE G_guard.true_enter";
		CALL clearInterlock ON SELF;;
	}
	
	COMMAND clearLock {
#		LOG "clearLock";
		IF (status IS interlocked_locked) {
			SET status TO interlocked;
		};
		IF (SELF != interlocked) {
#			LOG "RECEIVE C_dump.off_enter";
			IF (status IS on) {
				# this occurs when someone else turns the dump off when we were using it
				SET SELF TO off;
				CALL turnOn ON SELF;
				SET SELF TO on;
			};
			IF (status IS onpart) {
				# this occurs when someone else turns the dump off when we were using it
				SET SELF TO off;
				CALL turnOnPart ON SELF;
				SET SELF TO onpart;
			};
			IF (status IS locked || status IS off) {
				SET SELF TO off;
				SET status TO off;
			};
		};
	}

# when the dump is off, we have a chance to grab it and return 
# to our action
	RECEIVE C_dump.off_enter {
#		LOG "C_Dump.off_enter";
		CALL clearLock ON SELF;
	}

	RECEIVE C_dump.on_enter {
#		LOG "C_dump.on_enter";
		IF (status IS off) {
			SET SELF TO locked;
		};
		IF (status IS interlocked) {
			SET status TO interlocked_locked;
		}
	}

	RECEIVE C_dump.onpart_enter {
#		LOG "C_dump.onpart_enter";
		IF (status IS off) {
			SET SELF TO locked;
		};
		IF (status IS interlocked) {
			SET status TO interlocked_locked;
		}
	}

	# transitions relating to the on state.
	TRANSITION on,onpart TO off USING turnOff;
	TRANSITION off TO on USING turnOn;
	TRANSITION off TO onpart USING turnOnPart;
	TRANSITION on TO onpart USING SlowToOnPart;
	TRANSITION interlocked TO ANY REQUIRES G_guard IS true;
	TRANSITION locked TO on REQUIRES C_dump IS off;
	TRANSITION locked TO onpart REQUIRES C_dump IS off;
#	TRANSITION locked TO off REQUIRES C_dump IS off;
}

DUMP MACHINE dumpfull, dumppart {
	OPTION offdelay 5;

	off WHEN dumpfull IS off && dumppart IS off;
	on WHEN	dumpfull IS on && dumppart IS off;
	onpart WHEN dumpfull IS off && dumppart IS on;
	unknown DEFAULT;

	COMMAND turnOff {
#		LOG "turnOff";
		SET dumpfull TO off;
		SET dumppart TO off;
		WAIT offdelay;
	}

	COMMAND turnOn {
#		LOG "turnOn";
		SET dumpfull TO on;
	}

	COMMAND turnOnPart {
#		LOG "turnOnPart";
		SET dumppart TO on;
		SET dumpfull TO off;
	}

	RECEIVE dumpfull.on_enter { 
	# LOG "dumpfull.on_enter"
	 }
	RECEIVE dumpfull.off_enter {
	# LOG "dumpfull.off_enter"
	}
	RECEIVE dumppart.on_enter {
	# LOG "dumppart.on_enter"
	}
	RECEIVE dumppart.off_enter {
	# LOG "dumppart.off_enter"
	}

	TRANSITION off TO on ON turnOn;
	TRANSITION off,on TO onpart ON turnOnPart;
	TRANSITION on,onpart TO off ON turnOff;
}


