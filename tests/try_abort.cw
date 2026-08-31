# TRY blocking body + timeout -> ABORT (unsuccessful; the statement after the
# TRY must NOT run). Pass = "should not run" is absent.

TryAbort MACHINE {
    OPTION timeout 300;
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        }
        WHEN TIMER >= timeout {
            ABORT;
        }
        LOG "should not run";
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

a TryAbort;
