# A counter that automatically resets on overflow

CyclingCounter MACHINE {
    OPTION count 0;
    OPTION max_count 100;
    
    idle DEFAULT;
    overflow WHEN count >= max_count;
 
    counting DURING increment {
        count := count + 1;
    }
    
    COMMAND reset {
        count := 0;
    }
    
    ENTER overflow {
        LOG "Counter overflow, resetting";
        count := 0; # automatically falls back to idle
    }
}
