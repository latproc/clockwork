# This demonstrates a table of JSON records and a processor
# that extracts values from the records.

Table MACHINE {
    OPTION json_data JSON_VALUE [
       {"a": 1, "b": 2},
       {"a": 3, "b": 4}
    ];
    data LIST;

    ENTER INIT {
      PUSH ITEMS FROM json_data TO data;
    }

    COMMAND reset {
      CLEAR data;
      PUSH ITEMS FROM json_data TO data;
    }
}

Processor MACHINE tbl {
    OPTION record JSON_VALUE {};
    OPTION a "";
    OPTION b "";

    extract WHEN SELF IS update;
    update WHEN tbl.data IS NOT empty;
    idle DEFAULT;

    ENTER extract {
        a := ITEM ${a} OF record;
        b := ITEM ${b} OF record;
        LOG record AS STRING + " " + a + " " + b
    }
    ENTER update {
        record := TAKE FIRST FROM tbl.data;
    }
}

table Table;
processor Processor table;

# Note: the following should fail because the value is not JSON
#       but currently it just pushes the string as a single item
BadExampleNotJson MACHINE {
    OPTION value "test";
    data LIST;
    ENTER INIT { PUSH ITEMS FROM value TO data; }
}

# Note: The following fails because the value is not a JSON array.
BadExampleNotJsonArray MACHINE {
    OPTION value JSON_VALUE {"a": 1, "b": 2};
    data LIST;
    ENTER INIT { PUSH ITEMS FROM value TO data; }
}

should_fail1 BadExampleNotJson;
should_fail2 BadExampleNotJsonArray;
