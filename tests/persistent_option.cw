# PERSISTENT OPTION: a per-field persist.dat marker (not a database column,
# not LOCAL). Parse test: `cw --parse-only persistent_option.cw` must exit 0.

Setpoint MACHINE {
    PERSISTENT OPTION sp 0;   # persist.dat only
    OPTION pv 0;              # RAM (unless this class is TABLE-bound)
}

s Setpoint;

Customer RECORD {
    OPTION id 0 KEY;
    OPTION name "";
    PERSISTENT OPTION note "";   # a non-column field that survives restart
}

cust Customer;
