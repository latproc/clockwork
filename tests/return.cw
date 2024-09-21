# Example of return

return_example ReturnExample;
test_return_driver ReturnTestDriver return_example;

ReturnExample MACHINE {
    OPTION value 0;

    COMMAND inc {
        INC value;
        RETURN;
        INC value; # This will not be executed
    }

    COMMAND reset {
        value := 0;
    }
}

ReturnTestDriver MACHINE other {

    idle DEFAULT;
    idle INITIAL;

    error WHEN other.value > 1;
    ok WHEN other.value == 1;
    ready WHEN other.value == 0;

    COMMAND run WITHIN ready {
        SEND inc TO other;
    }

    COMMAND reset {
        SEND reset TO other;
    }

    ENTER ok {
       LOG "return test passed";
    }

    ENTER error {
       LOG "return test failed";
    }
}
