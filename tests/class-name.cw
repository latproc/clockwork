# examples of the CLASS OF clause

list LIST;
flag FLAG;
class_name_test TestMachine;
play Playground;

Values MACHINE {
    OPTION int 1;
    OPTION float 1.0;
    OPTION string "example";
    OPTION json_array JSON_VALUE [1, 2, 3];
    OPTION json_object JSON_VALUE {"key": "value"};

    ok STATE;

    COMMAND check {
        LOG "list: " + CLASS OF list;
        LOG "flag: " + CLASS OF flag;
        LOG "int: " + CLASS OF int;
        LOG "float: " + CLASS OF float;
        LOG "string: " + CLASS OF string;
        LOG "json_array: " + CLASS OF json_array;
        LOG "json_object: " + CLASS OF json_object;
        SET SELF TO ok;
    }
}

TestMachine MACHINE {
    values Values;

    error WHEN SELF IS error || (SELF IS INIT && TIMER > 5000);
    ok WHEN values IS ok;

    ENTER INIT { SEND check TO values; }
}

Playground MACHINE {
    OPTION json JSON_VALUE {};

    json_object WHEN CLASS OF json IS "JSON_OBJECT";
    json_array WHEN CLASS OF json IS "JSON_ARRAY";
    int WHEN CLASS OF json IS "INTEGER";
    float WHEN CLASS OF json IS "FLOAT";
    string WHEN CLASS OF json IS "STRING";
    symbol WHEN CLASS OF json IS "SYMBOL";
}
