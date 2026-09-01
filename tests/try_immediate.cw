# TRY where the timeout predicate is already true (TIMER >= 0): the handler
# fires immediately without waiting for a timer.

TryImmediate MACHINE {
    OPTION timeout 0;
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        }
        WITH TIMEOUT timeout ON TIMEOUT {
            LOG "immediate timeout";
            SHUTDOWN;
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

i TryImmediate;
