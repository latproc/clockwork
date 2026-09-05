# RECEIVE handler with WITH TIMEOUT: a handler body that blocks past its deadline
# fires the ON TIMEOUT block (handler-level deadline, timeout-spec.md).
#
# Pass: "receive timed out" is logged.
# Fail: the message never appears.

ReceiveTimeoutRuntime MACHINE {
    OPTION never 0;

    idle DEFAULT;

    RECEIVE trigger {
        WAITFOR never == 1;
    } WITH TIMEOUT 300 ON TIMEOUT {
        LOG "receive timed out";
        SHUTDOWN;
    }

    ENTER INIT {
        SEND trigger TO SELF;
    }
}

r ReceiveTimeoutRuntime;
