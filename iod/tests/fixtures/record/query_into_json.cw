Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

all LIST;
ed MACHINE {
    COMMAND refresh {
        QUERY JSON_VALUE {
            "action": "select", "type": "customer", "auth": "xxx",
            "where": {"name": {"like": "A%"}}, "order": ["name"]
        } INTO all;
    }
}
