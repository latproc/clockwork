# Line A: an LPC COMMANDCLOCK MACHINE body is still allowed (it replaces the
# builtin class). Load must succeed; dispatch remains opt-in via dispatch:1.

COMMANDCLOCK MACHINE Guard {
  OPTION notify_period 1000;
  OPTION command "calcAdjust";
  OPTION dispatch 1;
  off LOCAL STATE;
  on LOCAL STATE;
  off WHEN Guard DISABLED;
  off WHEN Guard IS "false" || Guard IS "off";
  on DEFAULT;
  off INITIAL;
}

COMMANDCLOCKGUARD MACHINE {
  true DEFAULT;
  true INITIAL;
}

clock_guard COMMANDCLOCKGUARD;
clock COMMANDCLOCK (notify_period:100) clock_guard;
