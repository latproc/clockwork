OnDurationCounter MACHINE input {
    OPTION total_on 0;
    OPTION total_off 0;
    OPTION last_event 0;
    OPTION final_on_time 0;
    OPTION final_off_time 0;
    OPTION off_stable_time 1000;

    active_on WHEN SELF IS active_on;
    active_off WHEN SELF IS active_off,
        EXECUTE done WHEN TIMER > off_stable_time;
    idle_off DEFAULT;

    COMMAND done { 
        final_on_time := total_on;  
        final_off_time := total_off;  
        SET SELF TO idle_off;
    }

    # this command is used for the idle->on transition
    switchingOn DURING start { last_event := NOW; total_on := 0; total_off := 0; }

    # this command is used for the off->on transition
    switchingOn DURING switchOn {
        total_off := NOW - last_event; 
        last_event := NOW; 
        SET SELF TO active_on; 
    }
    
    switchingOff DURING switchOff {
        total_on := NOW - last_event; 
        last_event := NOW;
        SET SELF TO active_off;
    }


    RECEIVE input.on_enter { SET SELF TO active_on; }
    RECEIVE input.off_enter { SET SELF TO active_off; }

    TRANSITION idle_off TO active_on USING start;
    TRANSITION active_off TO active_on USING switchOn;
    TRANSITION active_on TO active_off USING switchOff;
}

in FLAG;
test OnDurationCounter in;
