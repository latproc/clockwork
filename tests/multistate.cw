States MACHINE {
  one INITIAL;
  two STATE;
  three STATE;
}

s1 States;
s2 States;
AllStates LIST s1,s2;

Test MACHINE list {
  ok FLAG;

  on WHEN ok IS on && COUNT one FROM list + COUNT two FROM list == SIZE OF list;
  off DEFAULT;
}
test Test AllStates;

