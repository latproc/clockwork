# Check that humid sends properties with correct types

shared Shared;
Shared MACHINE {
    OPTION to_humid "";
    OPTION from_humid "";
    OPTION humid_value "";
    OPTION humid_int_value 0;
    OPTION humid_float_value 0;
    EXPORT READONLY STRING 40 to_humid;
    EXPORT READWRITE STRING 40 from_humid;
    EXPORT READONLY STRING 40 humid_value;
    EXPORT READONLY 32BIT humid_int_value;
    EXPORT READONLY FLOAT32 humid_float_value;

    waiting WHEN humid_value == from_humid;
    string WHEN CLASS OF from_humid == "STRING";
    integer WHEN CLASS OF from_humid == "INTEGER";
    float WHEN CLASS OF from_humid == "FLOAT";
    boolean WHEN CLASS OF from_humid == "BOOLEAN";
    symbol WHEN CLASS OF from_humid == "SYMBOL";
    idle DEFAULT;

    COMMAND sync {
        humid_value := from_humid;
        humid_int_value := 0;
    }

    COMMAND sync_int {
        humid_value := "";
        humid_int_value := from_humid;
        humid_float_value := 0.0;
    }

    COMMAND sync_float {
        humid_value := "";
        humid_int_value := 0;
        humid_float_value := from_humid;
    }

    ENTER integer { to_humid := "INTEGER"; CALL sync_int; }
    ENTER float { to_humid := "FLOAT"; CALL sync_float; }
    ENTER boolean { to_humid := "BOOLEAN"; CALL sync; }
    ENTER string { to_humid := "STRING"; CALL sync; }
    ENTER idle { LOG "class of from_humid: " + CLASS OF humid_value; }
    ENTER symbol { to_humid := "SYMBOL"; CALL sync; }
}
