# This is an example of how we can call a function from a value
# stored in a property.

# Given one or more machines that have a function that we want to call.
Function MACHINE {
    COMMAND hello { LOG("hello"); }
    COMMAND test  { LOG("test"); }
}

# Create a machine for each function that we want to call.
CallHello MACHINE target { OPTION f "hello"; COMMAND go{ LOG "calling hello"; CALL hello ON target; } }
CallTest  MACHINE target { OPTION f "test";  COMMAND go{ LOG "calling test"; CALL test  ON target; } }

# Place the caller machines into a list.
callers LIST m_hello, m_test;

# Create a control machine that will pull a machine from a list and use CALL to 
# invoke the named function.

Control MACHINE commands {
    temp LIST;
    x REFERENCE;
    OPTION command "";
    COMMAND call {
        CLEAR temp;
        COPY ALL FROM commands TO temp WHERE commands.ITEM.f == command;
        MOVE 1 FROM temp TO x;
        CALL go ON x.ITEM;
    }
}

# To invoke the CALL, we set the command property to the name of the function
# and either SEND or CALL the `call` command on the control machine.

# Instantiate machines
f Function;
m_hello CallHello f;
m_test  CallTest  f;
control Control callers;
