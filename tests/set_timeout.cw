# SET ... WITH TIMEOUT <duration> ON TIMEOUT { }: parse test. A synchronous SET
# completes before it suspends, so its deadline has no observable effect
# (timeout-spec.md). `cw --parse-only set_timeout.cw` must exit 0.

SetTimeout MACHINE {
    OPTION x 0;

    idle DEFAULT;

    COMMAND run {
        SET x TO 1 WITH TIMEOUT 300 ON TIMEOUT { LOG "set timed out"; }
    }
}

t SetTimeout;
