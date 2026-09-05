# TRY { } WITH TIMEOUT ... ON TIMEOUT { } ON ERROR { }: a timeout runs ON TIMEOUT
# and does NOT run ON ERROR (a timeout is a distinct outcome from an error).
#
# Pass: "timed out" is logged.
# Fail: "should not error" is logged (or neither).

TryOnErrorTimeout MACHINE {
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        } WITH TIMEOUT 300 ON TIMEOUT {
            LOG "timed out";
            SHUTDOWN;
        } ON ERROR {
            LOG "should not error";
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TryOnErrorTimeout;
