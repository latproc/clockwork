CustomerWithCity RECORD VIEW "customer_with_city" {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION city "";
}

all LIST;
ed MACHINE {
    COMMAND refresh {
        CLEAR all;
        COPY ALL FROM CustomerWithCity TO all;
    }
}
