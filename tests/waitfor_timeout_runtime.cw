# Runtime test: a WAITFOR that never completes fires ON TIMEOUT THROW, and the
# message is caught by CATCH. The command's `(TIMEOUT : 300)` supplies the
# duration (ms).
#
# Pass: "waitfor timed out" is logged (then SHUTDOWN).
# Fail: the message never appears (WAITFOR hangs).

TimeoutRuntime MACHINE {
    OPTION never 0;

    CATCH tmo {
        LOG "waitfor timed out";
        SHUTDOWN;
    }

    idle DEFAULT;

    COMMAND run (TIMEOUT : 300) {
        WAITFOR never == 1 ON TIMEOUT THROW tmo;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TimeoutRuntime;
