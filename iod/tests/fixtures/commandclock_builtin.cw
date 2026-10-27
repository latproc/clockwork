# Line A: COMMANDCLOCK is a recognised type. Dispatch is opt-in via dispatch:1.
# This file has no COMMANDCLOCK MACHINE body; the builtin class must be enough
# for cw -t / iod load (no "class COMMANDCLOCK not found").

COMMANDCLOCKGUARD MACHINE {
  true DEFAULT;
  true INITIAL;
}

clock_guard COMMANDCLOCKGUARD;
clock COMMANDCLOCK (notify_period:100) clock_guard;

COMMANDCLOCKCONSUMER MACHINE Clock {
  OPTION ticks 0;
  idle DEFAULT;
  idle INITIAL;
  COMMAND calcAdjust {
    INC ticks;
    LOG "unexpected COMMANDCLOCK dispatch without opt-in";
  }
}

clock_consumer COMMANDCLOCKCONSUMER clock;
