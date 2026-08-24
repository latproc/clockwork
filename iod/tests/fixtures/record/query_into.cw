Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

all LIST;
ed MACHINE {
    OPTION q JSON_VALUE {
        "action": "find", "type": "customer", "auth": "xxx", "keys": {}
    };
    COMMAND refresh {
        QUERY q INTO all;
    }
}
