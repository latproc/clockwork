# Runtime test: TIMEOUT is the read-only contextual value (the expired duration,
# ms) inside an ON TIMEOUT block.
#
# Pass: "timed out after 500ms" is logged.

TimeoutValueRuntime MACHINE {
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        WAITFOR never == 1
            WITH TIMEOUT 500
            ON TIMEOUT {
                LOG "timed out after " + TIMEOUT + "ms";
                SHUTDOWN;
            }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TimeoutValueRuntime;
