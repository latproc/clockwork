Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

all LIST;
ed MACHINE {
    COMMAND refresh {
        CLEAR all;
        COPY ALL FROM Customer TO all WHERE ITEM.name == "Ann";
    }
}
