# TRY body completes before the timeout: the handler must NOT run.
# The body's WAITFOR is already satisfied, so "body done" logs and the handler
# ("timed out") does not.

TryComplete MACHINE {
    OPTION timeout 5000;
    OPTION x 1;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR x == 1;
            LOG "body done";
        }
        WITH TIMEOUT timeout ON TIMEOUT {
            LOG "timed out";
        }
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

c TryComplete;
