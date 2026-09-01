# Runtime test: an ENTER INIT handler that blocks (WAITFOR never == 1) has an
# AFTER <duration> { … } deadline; when it expires the handler runs and SHUTDOWNs.
#
# Pass: "init timed out" is logged.
# Fail: the message never appears (the INIT handler hangs).

HandlerTimeoutRuntime MACHINE {
    OPTION never 0;

    idle DEFAULT;

    ENTER INIT {
        WAITFOR never == 1;
    } AFTER 500 {
        LOG "init timed out";
        SHUTDOWN;
    }
}

t HandlerTimeoutRuntime;
