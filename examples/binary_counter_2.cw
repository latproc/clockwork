# Demonstrates how T Type flip flops linked 
# together to create a binary counter

# This version has the TFF trigger off the falling
# edge of the input so it doesn't need inverters

Pulser MACHINE {
  OPTION delay 1000;
  on WHEN TIMER > delay;
  off DEFAULT;
}

TFF MACHINE in {
  on STATE;
  off INITIAL;

  RECEIVE in.off_enter { SEND next TO SELF; }
  TRANSITION on TO off ON next;
  TRANSITION off TO on ON next;
}

pulse Pulser;
one TFF pulse;
two TFF one;
three TFF two;
four TFF three;

# the following watches the above counters and
# collates the bits into a numeric value for
# easier display/test

res LIST four, three, two, one;
Monitor MACHINE list {
  OPTION VALUE 0;
  update WHEN VALUE != BITSET FROM list;
  idle DEFAULT;
  ENTER update { WAIT 20; VALUE := BITSET FROM list; }
}
monitor Monitor res;
