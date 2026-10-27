# COMMANDCLOCK cadence is driven by the Clockwork runtime when no EtherCAT
# transport exists. The consumer must declare calcAdjust and list the clock.

COMMANDCLOCK MACHINE Guard {
  OPTION notify_period 100;
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
analog_clock COMMANDCLOCK (notify_period:100, command:"update", dispatch:1) clock_guard;

COMMANDCLOCKCONSUMER MACHINE Clock {
  OPTION ticks 0;

  idle DEFAULT;
  idle INITIAL;

  COMMAND calcAdjust {
    INC ticks;
    LOG "COMMANDCLOCK calcAdjust tick";
  }
}

clock_consumer COMMANDCLOCKCONSUMER clock;

COMMANDCLOCKUPDATECONSUMER MACHINE Clock {
  idle DEFAULT;
  idle INITIAL;

  RECEIVE update {
    LOG "COMMANDCLOCK update tick";
  }
}

update_consumer COMMANDCLOCKUPDATECONSUMER analog_clock;
