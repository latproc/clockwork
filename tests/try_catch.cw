# TRY { ... } WHEN TIMER >= timeout { ... } + CATCH
#
# The TRY body blocks on a WAITFOR that never completes; after `timeout` ms the
# WHEN predicate fires, the handler THROWs, and CATCH handles it.

TryTest MACHINE {
    OPTION timeout 300;
    OPTION never 0;

    CATCH tmo {
        LOG "try timed out";
        SHUTDOWN;
    }

    idle DEFAULT;

    COMMAND run {
        TRY {
            WAITFOR never == 1;
        }
        WHEN TIMER >= timeout {
            THROW tmo;
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t TryTest;
