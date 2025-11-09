# an example of how to procvess JSON records

example_json VARIABLE JSON_VALUE [{"key": "value1"}, {"key": "value2"}, {"key": "value3"}];
record_index VARIABLE 0;
record_processor RecordProcessor example_json, record_index;

demo JSONArrayProcessor example_json, record_processor, record_index;

RecordProcessor MACHINE array, index {
   OPTION record JSON_VALUE {};

   COMMAND process {
       LOG "before: " + array AS STRING;
       record := ITEM ${[@index]} OF array;
       ITEM ${key} OF record := "updated " + (index+1) AS STRING;
       ITEM ${[@index]} OF array := record;
       LOG "after: " + array AS STRING;
    }
}

JSONArrayProcessor MACHINE json, processor, index {
    OPTION current NULL;

    error WHEN CLASS OF json.VALUE IS NOT JSON_ARRAY;
    ENTER error {
        LOG "Error: expected a JSON array but got " + CLASS OF json.VALUE AS STRING;
        SHUTDOWN;
    }

    process_all WHEN SELF IS idle AND CLASS OF current IS NOT EMPTY;
    idle DEFAULT;
    
    ENTER INIT {
        CALL update ON SELF;
    }

    COMMAND update {
        current := ITEM ${[@index]} OF json;
    }

    ENTER process_all {
        processor.record := current;
        CALL process ON processor;
        INC index;
        CALL update ON SELF;
    }
}
