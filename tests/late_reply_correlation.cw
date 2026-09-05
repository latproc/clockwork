# Late-reply correlation test (timeout-spec.md): a CALL that times out must not
# let its target's late _done reply prematurely complete a later CALL to the
# same command.
#
# Timeline (correct): find#1 starts (WAIT 1000); at 300ms the first CALL times
# out; the second CALL sends find again; find#1 completes at 1000ms
# (completions=1); find#2 completes at 2000ms (completions=2); only then does
# the second CALL return.
#
# Bug: find#1's late _done reply (1000ms) fires the second CALL's trigger,
# returning it early while worker.completions is still 1.
#
# Pass: "second call done completions=2" is logged.
# Fail: "second call done completions=1" is logged (early return).

Worker MACHINE {
    OPTION completions 0;
    OPTION seq 0;

    idle DEFAULT;

    COMMAND find {
        LOG "find start seq=" + seq;
        seq := seq + 1;
        WAIT 1000;
        completions := completions + 1;
        LOG "find done completions=" + completions;
    }
}

Caller MACHINE worker {
    idle DEFAULT;

    COMMAND run {
        CALL find ON worker WITH TIMEOUT 300 ON TIMEOUT {
            LOG "first call timed out";
        };
        CALL find ON worker;
        LOG "second call done completions=" + worker.completions;
        SHUTDOWN;
    }

    ENTER INIT {
        SEND run TO SELF;
    }
}

w Worker;
caller Caller w;
