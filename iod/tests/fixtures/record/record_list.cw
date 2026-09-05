Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

all LIST;
cust Customer;
ed MACHINE {
    COMMAND refresh {
        CLEAR all;
        COPY ALL FROM Customer TO all;
    }
}
