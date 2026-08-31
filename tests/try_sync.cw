# TRY with a synchronous (non-blocking) body: the body runs, the handler does
# not. Pass = "body ran" present, "timed out" absent.

TrySync MACHINE {
    OPTION timeout 5000;

    idle DEFAULT;

    COMMAND run {
        TRY {
            LOG "body ran";
        }
        WHEN TIMER >= timeout {
            LOG "timed out";
        }
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

s TrySync;
