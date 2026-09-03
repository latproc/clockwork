# OPTION PERSISTENT true (machine-level flag) is not valid on a RECORD.

Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
    OPTION PERSISTENT true;
}
cust Customer;
