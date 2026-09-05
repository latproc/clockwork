# A handler with ON TIMEOUT (no own WITH TIMEOUT) catches an inner statement's
# unhandled timeout; TIMEOUT reflects the inner deadline's duration (2000ms),
# not the handler's (which has none). timeout-spec.md "The TIMEOUT Value".
#
# Pass: "run timed out after 2000ms" is logged.
# Fail: the message shows a different value (e.g. 0ms).

HandlerTimeoutValue MACHINE {
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        WAITFOR never == 1 WITH TIMEOUT 2000;
    } ON TIMEOUT {
        LOG "run timed out after " + TIMEOUT + "ms";
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t HandlerTimeoutValue;
