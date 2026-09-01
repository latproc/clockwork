# Handler-level deadlines per timeout-spec.md: a handler body followed by
# WITH TIMEOUT <duration> [ ON TIMEOUT { … } ] or the AFTER <duration> { … }
# shorthand. Parse test: `cw --parse-only handler_timeout.cw` must exit 0.

TimeoutTest MACHINE {
    OPTION never 0;
    OPTION timeout 500;

    idle DEFAULT;

    COMMAND run {
        WAITFOR never == 1;
    } WITH TIMEOUT 500 ON TIMEOUT {
        LOG "run timed out";
    }

    ENTER idle AFTER timeout {
        LOG "idle timed out";
    }
}

t TimeoutTest;
