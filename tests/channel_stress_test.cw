# Multi-daemon channel stress test for memory leak detection
# Run with two separate iod instances:
#   Terminal 1: iod -cp 7910 tests/channel_stress_test.cw tests/stdchannels.cw
#   Terminal 2: iod -cp 7920 tests/channel_stress_secondary.cw tests/stdchannels.cw
#
# Monitor memory usage: watch -n 1 'ps aux | grep iod | grep -v grep'
# Or use Valgrind: valgrind --tool=memcheck --leak-check=full ./iod -cp 7910 tests/channel_stress_test.cw tests/stdchannels.cw

# Test channel for connecting daemons
STRESS_TEST_CHANNEL CHANNEL {
    OPTION host "localhost";
    OPTION port 7930;
    MONITORS `.*`;
    PUBLISHER;
    IGNORES `^SYSTEM`;
}

SAMPLER_CHANNEL CHANNEL {
	OPTION host "localhost";
	OPTION port 10610;
	KEY "be733dd278cd18825883a25f0e7c1b10";
	VERSION "0.1.0";
	MONITORS `.*`;
	IGNORES `^SYSTEM`;
	PUBLISHER;
}

# High-frequency state generator to stress the channel
stress_counter_1 CyclingCounter;
stress_counter_2 CyclingCounter; 
stress_counter_3 CyclingCounter;
stress_counter_4 CyclingCounter;
stress_counter_5 CyclingCounter;
counters LIST stress_counter_1, stress_counter_2, stress_counter_3, stress_counter_4, stress_counter_5;
stress_generator StressGenerator counters;

StressGenerator MACHINE counters {
    OPTION cycle_count 0;
    OPTION target_cycles 10000;  # Adjust for longer stress test
    started FLAG; # initially off
    
    done WHEN cycle_count >= target_cycles;
    running WHEN started IS on AND SELF IS idle AND TIMER > 10;
    idle DEFAULT;
    
    COMMAND start {
        LOG "Starting stress test with " + target_cycles + " cycles";
        cycle_count := 0;
        SET started TO on;
    }

    COMMAND stop { SET started TO off; }
    
    ENTER done {
        LOG "Stress test completed after " + cycle_count + " cycles";
    }
    
    ENTER running {  # Generate state changes every 10ms
        cycle_count := cycle_count + 1;
        
        # Trigger state changes in multiple machines
        SEND increment TO counters;
    }
}

# Test driver to coordinate the stress test
stress_test_driver StressTestDriver stress_generator;

StressTestDriver MACHINE generator {
    OPTION test_duration 60000;  # 60 seconds in ms
    OPTION start_time 0;
    
    completed WHEN generator IS done OR (SELF IS running AND TIMER - start_time >= test_duration);
    running WHEN start_time != 0;
    idle INITIAL;
    idle DEFAULT;
    
    COMMAND start WITHIN idle {
        LOG "Setting up stress test";
        start_time := TIMER;
    }
    
    ENTER running {
        LOG "Starting stress generators";
        SEND start TO generator;
    }
    
    ENTER completed {
        LOG "Test duration exceeded, stopping generators";
        SEND stop TO generator;
        SET SELF TO monitoring;
    }
}