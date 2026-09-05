# TRY blocking body + timeout -> RETURN (successful completion, no re-evaluation).
# The statement after the TRY runs, proving RETURN completed the TRY normally.

TryReturn MACHINE {
    OPTION timeout 300;
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        }
        WITH TIMEOUT timeout ON TIMEOUT {
            RETURN;
        }
        LOG "returned normally";
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

r TryReturn;
