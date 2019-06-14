# Demonstrates how T Type flip flops linked 
# together to create a binary counter

# this is the traditional version, using an inverter
# at each output to be fed into the next TFF

Pulse MACHINE {
  OPTION delay 1000;
  on WHEN TIMER > delay;
  off DEFAULT;
}
Invert MACHINE in {
  on WHEN in IS off;
  off WHEN in IS on;
}
TFF MACHINE in {
  on STATE;
  off INITIAL;

  RECEIVE in.on_enter { SEND next TO SELF; }
  TRANSITION on TO off ON next;
  TRANSITION off TO on ON next;
}

pulse Pulse;
one TFF pulse;
one_ Invert one;
two TFF one_;
two_ Invert two;
three TFF two_;
three_ Invert three;
four TFF three_;

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
