# Check whether an ABORT nested inside an IF correctly aborts the 
# current handler.

# NOTE: This test is known to fail. ABORT does not currently abort the current
# handler, but instead just exits the current block.

AbortCheck MACHINE {
    OPTION a "A";
    OPTION b "B";

    error WHEN b == "D";
    check WHEN a == "A" AND b == "B";
    idle DEFAULT;

    ENTER check {
        IF (a == "A") {
            b := "C";
            ABORT;
        };
        b := "D";
    }

    ENTER error {
        LOG "check should have aborted but continued";
    }

}

check AbortCheck;
