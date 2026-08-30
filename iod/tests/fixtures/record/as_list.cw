Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

all LIST;
ed MACHINE {
    OPTION result JSON_VALUE [{"id":1,"name":"Ann"},{"id":2,"name":"Bob"}];
    COMMAND refresh {
        all := result AS LIST;
    }
}
