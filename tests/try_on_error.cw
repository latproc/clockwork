# TRY { } WITH TIMEOUT ... ON ERROR { }: a body that fails (CALL to a missing
# machine) runs ON ERROR, not ON TIMEOUT.
#
# Pass: "error handled" is logged.
# Fail: "should not timeout" is logged (or neither).

TryOnError MACHINE {
    idle DEFAULT;

    COMMAND run {
        TRY {
            CALL nothing ON missing_machine;
        } WITH TIMEOUT 5000 ON TIMEOUT {
            LOG "should not timeout";
        } ON ERROR {
            LOG "error handled";
            SHUTDOWN;
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TryOnError;
