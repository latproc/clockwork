Timer MACHINE {
    started FLAG;
    
    running WHEN started IS on;
    idle DEFAULT;

    RECEIVE stop { timer := TIMER; }
}

Flash MACHINE output(delay:500) {
    OPTION tab Outputs;

    timer Timer(tab:Outputs);

    started_flag FLAG (type:FLAG, tab:Outputs);
    error_flag FLAG (type:FLAG, tab:Outputs);

#    timed_out WHEN started_flag IS on && TIMER >= 1000;
    on WHEN started_flag != off && output IS on;
    off WHEN started_flag != off && output IS off;
    error WHEN error_flag IS on;

    ready DEFAULT;

    starting DURING start { SET started_flag TO on; }
    stopping DURING stop { SET started_flag TO off; }

    TRANSITION ready TO on USING start;
    TRANSITION on,off TO ready USING stop;

    RECEIVE on_enter {
        LOG "on";
        WAIT output.delay;
        SET output TO off ;
        SEND stop TO timer;
    }
    RECEIVE off_enter { LOG "off"; WAIT output.delay; SET output TO on; time := timer.timer; SEND start TO timer; }
}

out FLAG;
flash Flash out;
