# Handler-level ON ERROR (no WITH TIMEOUT): a COMMAND whose body fails runs its
# ON ERROR block. The handler catches an inner error without its own deadline
# (timeout-spec.md "ON TIMEOUT Scope" / "Interaction With Errors").
#
# Pass: "handler error caught" is logged.
# Fail: the message never appears.

HandlerOnError MACHINE {
    idle DEFAULT;

    COMMAND run {
        CALL nothing ON missing_machine;
    } ON ERROR {
        LOG "handler error caught";
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

h HandlerOnError;
