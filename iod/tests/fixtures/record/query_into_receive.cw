Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

all LIST;
ed MACHINE {
    OPTION response JSON_VALUE {};
    OPTION q JSON_VALUE {
        "action": "select", "type": "customer", "auth": "xxx", "where": {}
    };
    COMMAND refresh {
        QUERY q INTO all;
    }
    # An explicit handler wins: the QUERY ... INTO fill must not be installed on
    # top of it, so this parses without a duplicate RECEIVES handler error.
    RECEIVE response_changed {
        all := response AS LIST;
    }
}
