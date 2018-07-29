Flasher MACHINE {

on WHEN SELF IS on, EXECUTE flip WHEN TIMER>=1000;
off WHEN SELF IS off, EXECUTE flip WHEN TIMER>=1000;

COMMAND flip { SEND next TO SELF }

TRANSITION on TO off ON next;
TRANSITION off TO on ON next;
}

flasher Flasher;
