Generator MACHINE {
    OPTION x 0;

    idle DEFAULT;
    waiting WHEN SELF IS INIT AND COUNT ACTIVE FROM CHANNELS == 0;

    next WHEN SELF IS idle AND TIMER >= 100;
    ENTER next {
        x := RANDOM % 100;
        LOG "Generated random number: " + x;
    }

    ENTER INIT {
        RANDOMSEED := 31415; # Set a fixed seed for reproducibility
    }
}
generator Generator;
