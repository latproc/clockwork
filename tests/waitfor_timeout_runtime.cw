# Runtime test: a WAITFOR that never completes fires ON TIMEOUT, and the THROW
# message is caught by CATCH. The WITH TIMEOUT <duration> is inline (ms).
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

    COMMAND run {
        WAITFOR never == 1 WITH TIMEOUT 300 ON TIMEOUT { THROW tmo; }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TimeoutRuntime;
