# Secondary daemon for multi-daemon channel stress testing
# Run with: iod -cp 7920 tests/channel_stress_secondary.cw tests/stdchannels.cw

# Connect to the same stress test channel 
STRESS_TEST_CHANNEL CHANNEL {
    OPTION host "localhost";
    OPTION port 7930;
    MONITORS `.*`;
    PUBLISHER;
    IGNORES `^SYSTEM`;
}

SAMPLER_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 10620;
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	MONITORS `.*`;
	IGNORES `^SYSTEM`;
	PUBLISHER;
}

# Secondary stress generators
secondary_counter_1 CyclingCounter;
secondary_counter_2 CyclingCounter;
secondary_counter_3 CyclingCounter;
secondary_counters LIST secondary_counter_1, secondary_counter_2, secondary_counter_3;
secondary_generator SecondaryStressGenerator secondary_counters;

SecondaryStressGenerator MACHINE counters {
    OPTION cycle_count 0;
    started FLAG;

    log WHEN SELF IS running AND cycle_count % 1000 == 0;
    running WHEN started IS on AND SELF IS idle AND TIMER > 15;
    idle DEFAULT;
     
    running STATE;
    idle INITIAL;
    idle DEFAULT;
    
    COMMAND start { SET started TO on }
    COMMAND stop  { SET started TO off }
    
    ENTER running {
        cycle_count := cycle_count + 1;
        SEND increment TO counters;
    }

    ENTER log {
        LOG "Secondary: " + cycle_count + " cycles completed";
    }
}

# Monitor for messages from primary daemon
secondary_monitor SecondaryMonitor secondary_generator;

SecondaryMonitor MACHINE generator {
    idle DEFAULT;

    # Auto-start when channel is available
    monitoring WHEN SIZE OF CHANNELS > 0;
 
    # CHANNELS doesn't propagate changes so a check state is needed
    checking_channels WHEN SELF IS idle AND TIMER > 10;
 
    COMMAND stop { SEND stop TO generator }
   
    ENTER monitoring {
        LOG "Secondary daemon monitoring channel traffic";
        SEND start TO generator;
    }
    
 }