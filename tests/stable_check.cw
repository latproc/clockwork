# examples of holding states. 
#
# sometimes it is desirable to force a machine into a state and have it
# hold there. This is permitted only if the predicate on that state 
# evaluates to true.


# cannot use SET hold TO on because the rule on this machine will not be true
Holding MACHINE {
  on WHEN SELF IS on;
  off DEFAULT;
}
hold Holding;

# can SEND turnOn TO holdcmd because the DURING takes care of the 
# state change to on, then the rule is true and the machine stays on
HoldCommand MACHINE {
  on WHEN SELF IS on;
  off DEFAULT;
  
  on DURING turnOn { }
  off DURING turnOff { }

}
holdcmd HoldCommand;

# cannot SET holdtran TO on because the rule is not valid. 
# there is no DURING clause on the turnOn to help
HoldTransition MACHINE {
  on WHEN SELF IS on;
  off DEFAULT;

  TRANSITION on TO off USING turnOn;
  TRANSITION off TO on USING turnOff;
}
holdtran HoldTransition;

# add manual states

# in this case SET hold TO on is valid because there is a manual STATE declaration for on
Holding2 MACHINE {
  on STATE;
  on WHEN SELF IS on;
  off DEFAULT;
}
hold2 Holding2;

# also in this case SET hold TO on is valid because there is a manual STATE declaration for on
HoldTransition2 MACHINE {
  on STATE;
  on WHEN SELF IS on;
  off DEFAULT;

  TRANSITION on TO off USING turnOn;
  TRANSITION off TO on USING turnOff;
}
holdtran2 HoldTransition2;
