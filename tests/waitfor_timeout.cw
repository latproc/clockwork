# WAITFOR / CALL ... ON TIMEOUT ABORT | RETURN | THROW <msg>
# with the command-level TIMEOUT <ms> property (Part B, Q11 / iod-15).
#
# Parse test: `cw --parse-only waitfor_timeout.cw` must exit 0.
# Runtime test: see tests/waitfor_timeout_runtime.cw.

TimeoutTest MACHINE {
    OPTION never 0;
    OPTION fired 0;

    CATCH tmo { LOG "waitfor timed out"; fired := 1; }

    idle DEFAULT;
    done WHEN fired == 1;

    COMMAND run_abort (TIMEOUT : 200) {
        WAITFOR never == 1 ON TIMEOUT ABORT;
    }
    COMMAND run_return (TIMEOUT : 200) {
        WAITFOR never == 1 ON TIMEOUT RETURN;
    }
    COMMAND run_throw (TIMEOUT : 200) {
        WAITFOR never == 1 ON TIMEOUT THROW tmo;
    }
    COMMAND run_call (TIMEOUT : 200) {
        CALL find ON self ON TIMEOUT THROW tmo;
    }
}

t TimeoutTest;
