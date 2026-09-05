# OPTION PERSISTENT true (machine-level flag) is not valid on a table-bound
# MACHINE (which persists via the table / per-field PERSISTENT OPTION).

CustomerPanel MACHINE TABLE "customer" {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION PERSISTENT true;
}
cust CustomerPanel (id: 1);
