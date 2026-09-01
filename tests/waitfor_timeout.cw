# WAITFOR / CALL ... WITH TIMEOUT <duration> ON TIMEOUT { <statements> }
# (per timeout-spec.md). Parse test: `cw --parse-only waitfor_timeout.cw` must
# exit 0. Runtime test: see tests/waitfor_timeout_runtime.cw.

TimeoutTest MACHINE {
    OPTION never 0;
    OPTION fired 0;
    OPTION timeout 200;

    CATCH tmo { LOG "waitfor timed out"; fired := 1; }

    idle DEFAULT;
    done WHEN fired == 1;

    COMMAND run_abort {
        WAITFOR never == 1 WITH TIMEOUT 200 ON TIMEOUT { ABORT; }
    }
    COMMAND run_return {
        WAITFOR never == 1 WITH TIMEOUT timeout ON TIMEOUT { RETURN; }
    }
    COMMAND run_throw {
        WAITFOR never == 1 WITH TIMEOUT 200 ON TIMEOUT { THROW tmo; }
    }
    COMMAND run_call {
        CALL find ON self WITH TIMEOUT 200 ON TIMEOUT { THROW tmo; }
    }
}

t TimeoutTest;
