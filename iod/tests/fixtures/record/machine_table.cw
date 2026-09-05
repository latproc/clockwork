Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
}

CustomerPanel MACHINE TABLE "customer" {
    OPTION id 0 KEY;
    OPTION name "";
    LOCAL OPTION state "empty";
    active WHEN name != "";
    idle DEFAULT;
}

cust CustomerPanel (id: 1);
