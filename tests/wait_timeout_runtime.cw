# WAIT <duration> WITH TIMEOUT <ms> ON TIMEOUT { }: a WAIT that would outlast its
# deadline fires the ON TIMEOUT block (timeout-spec.md statement forms).
#
# Pass: "wait timed out" is logged.
# Fail: the message never appears.

WaitTimeoutRuntime MACHINE {
    idle DEFAULT;

    COMMAND run {
        WAIT 5000 WITH TIMEOUT 300 ON TIMEOUT {
            LOG "wait timed out";
            SHUTDOWN;
        }
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

t WaitTimeoutRuntime;
