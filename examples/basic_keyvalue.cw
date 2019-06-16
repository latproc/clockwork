
test KEYVALUE;

KVTest MACHINE {
    idle INITIAL;
    COMMAND a { test.Key := "test"; }
    COMMAND b { test.Key := "test2"; }
}
