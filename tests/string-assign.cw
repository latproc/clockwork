# Is there a problem with assigning a string to an separate machine

aa A;
bb B;
cc C;

A MACHINE {
    OPTION s "Hello, World!";

    ok WHEN CLASS OF s IS "STRING";
    error DEFAULT;
}
B MACHINE {
    a A;
    OPTION t "";
    ok WHEN CLASS OF t == "STRING";
    error DEFAULT;
    ENTER INIT { t := a.s; }
    ENTER error { LOG "Error: string assigned from an external property is a " + CLASS OF t; }
}
C MACHINE {
    a A;
    b B;
    OPTION t "hello from C";
    ok WHEN CLASS OF t == "STRING";
    error DEFAULT;
    ENTER INIT {
        a.s := t;
        t := b.a.s;
    }
    ENTER error { LOG "Error: string assignments through external machines yielded: " + CLASS OF t; }
}

# Check extracting a string from JSON and passing it to another machine

Target MACHINE {
    OPTION str "";
    ok WHEN CLASS OF str == "STRING";
    error DEFAULT;
}

Source MACHINE {
    target Target;
    OPTION jsonData JSON_VALUE {"message": "Hello from JSON!"};
    ok WHEN CLASS OF jsonData == "JSON_OBJECT" AND CLASS OF target.str == "STRING";
    error DEFAULT;
    ENTER error { LOG "Error: string assignment from JSON failed, got " + CLASS OF target.str; }
    ENTER INIT {
        target.str := ITEM ${message} OF jsonData; 
    }
}

test_assign_from_json Source;
