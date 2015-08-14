# This file shows a couple of ways to implement a blinker
# the first technique does not use any automatic state 
# transitions and needs to be kick-started since the 
# machine powers-up in the (hidden) INIT state.

Blinker MACHINE {

  on STATE;
  off INITIAL;

  ENTER on { WAIT 1000; SEND turnOff TO SELF; }
  ENTER off { WAIT 1000; SEND turnOn TO SELF; }
  off DURING turnOff{}
  on DURING turnOn{}

}
blinker Blinker;

# here is an alternative method to blink, this machine 
# has an optional delay before starting but then it automatically
# moves to the off state and then alternates between the
# on and off states on a 1000ms interval.

AltBlinker MACHINE {
  OPTION DELAY 0;
  INIT WHEN SELF IS INIT;
  on WHEN SELF IS off AND TIMER >= 1000 || SELF IS on AND TIMER < 1000;
  off DEFAULT;

  ENTER INIT { WAIT delay; }
}
a AltBlinker(delay:200);
b AltBlinker(delay:400);
c AltBlinker(delay:600);

PINGGENERATOR MACHINE {
  OPTION DELAY 0;
  on WHEN SELF IS off AND TIMER >= 1000 || SELF IS on AND TIMER < 1000;
  off DEFAULT;
  ENTER INIT { WAIT delay; }
}

PING MACHINE Slave {
  OPTION StartDelay 0;
  OPTION OnTime 1000;
  OPTION OffTime 1000;
  on WHEN SELF IS off AND TIMER >= OffTime || SELF IS on AND TIMER < OnTime;
  off DEFAULT;
  ENTER on { SET Slave TO on; }
  ENTER off { SET Slave TO off; }
  ENTER INIT { WAIT StartDelay; }
}

ping PINGGENERATOR;

