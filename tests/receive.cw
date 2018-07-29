# Test that receiving messages from remote machines does
# not get confused with local event handlers

Test MACHINE {

flag FLAG;

on WHEN flag IS on AND flag.TIMER > 200;
off DEFAULT;

ENTER on { LOG "on"; }

RECEIVE flag.on_enter { LOG "flag came on" }

}

test Test;
