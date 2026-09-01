# TRY { } WITH TIMEOUT ... ON TIMEOUT { ABORT; } ON ERROR { }: an ABORT in the
# ON TIMEOUT block cascades to ON ERROR (timeout-spec.md "Interaction With
# Errors"), with the timeout context still available.
#
# Pass: "abort cascaded to error" is logged.
# Fail: the message never appears.

TryOnErrorCascade MACHINE {
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        } WITH TIMEOUT 300 ON TIMEOUT {
            ABORT;
        } ON ERROR {
            LOG "abort cascaded to error";
            SHUTDOWN;
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TryOnErrorCascade;
