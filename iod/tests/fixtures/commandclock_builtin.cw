# Line A: COMMANDCLOCK is a recognised type with no tick dispatch.
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
  }
}

clock_consumer COMMANDCLOCKCONSUMER clock;
