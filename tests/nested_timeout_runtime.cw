# Nested deadlines per timeout-spec.md: an inner timeout's recovery handler runs
# under the still-active outer deadline; when the outer deadline expires it
# cancels the inner recovery and runs the outer handler.
#
# Pass: "outer timeout" is logged (and "inner recovery" is not).
# Fail: the inner recovery hangs (WAITFOR never == 2) and the outer handler never runs.

NestedTimeoutRuntime MACHINE {
    OPTION never 0;

    idle DEFAULT;

    COMMAND run {
        TRY {
            TRY {
                WAITFOR never == 1;
            } WITH TIMEOUT 2000 ON TIMEOUT {
                WAITFOR never == 2;
                LOG "inner recovery";
            }
        } WITH TIMEOUT 4000 ON TIMEOUT {
            LOG "outer timeout";
            SHUTDOWN;
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t NestedTimeoutRuntime;
