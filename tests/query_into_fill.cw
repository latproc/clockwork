# query_into_fill.cw
#
# `QUERY ... INTO <list>` installs an automatic reply fill: when the dbsvr reply
# lands on the machine's `response` OPTION, dbd sends `response_changed`, and the
# named LIST is cleared and refilled from `response` with no explicit RECEIVE
# handler. A machine that declares its own `RECEIVE response_changed` keeps that
# handler and gets no synthetic fill.
#
# Runs without dbsvr: `response` already holds the reply array and the driver
# sends the same `response_changed` message dbd sends after routing a reply.
#
# Asserted by CMake with separate bounded runs:
#   runtime_query_into_fill_auto   must-contain "AUTO size=3"
#   runtime_query_into_fill_manual must-contain "MANUAL size=2"
#                                  must-not-contain "Already have a RECEIVES handler"

# No RECEIVE response_changed: QUERY ... INTO installs the fill.
AutoEd MACHINE {
    OPTION response JSON_VALUE [ {"a": 0}, {"a": 1}, {"a": 2} ];
    OPTION A0 -1;
    LISTX LIST;
    OPTION q JSON_VALUE {
        "action": "select", "auth": "xxx", "type": "seed", "fields": ["a"], "where": {}
    };
    COMMAND go { QUERY q INTO LISTX; }
    COMMAND count { A0 := SIZE OF LISTX; LOG "AUTO size=" + A0; }
}
auto_ed AutoEd;

# An explicit handler owns the reply, so no synthetic fill is installed. This
# must also parse cleanly (no "Already have a RECEIVES handler" error).
ManualEd MACHINE {
    OPTION response JSON_VALUE [ {"b": 0}, {"b": 1} ];
    OPTION A1 -1;
    LIST2 LIST;
    OPTION q JSON_VALUE {
        "action": "select", "auth": "xxx", "type": "seed", "fields": ["b"], "where": {}
    };
    COMMAND go { QUERY q INTO LIST2; }
    COMMAND count { A1 := SIZE OF LIST2; LOG "MANUAL size=" + A1; }
    RECEIVE response_changed { LIST2 := response AS LIST; }
}
manual_ed ManualEd;

Driver MACHINE {
    OPTION n 0;
    step WHEN SELF IS wait AND TIMER >= 300;
    wait DEFAULT;
    ENTER step {
        n := n + 1;
        IF (n == 1) {
            SEND response_changed TO auto_ed;
            SEND response_changed TO manual_ed;
        }
        IF (n == 2) {
            SEND count TO auto_ed;
            SEND count TO manual_ed;
        }
    }
}
driver Driver;
