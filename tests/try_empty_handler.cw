# TRY with an empty handler: on timeout the TRY just completes (the body is
# aborted, nothing else happens). The statement after the TRY runs.

TryEmptyHandler MACHINE {
    OPTION timeout 300;
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        }
        WITH TIMEOUT timeout ON TIMEOUT {
        }
        LOG "completed via empty handler";
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

e TryEmptyHandler;
