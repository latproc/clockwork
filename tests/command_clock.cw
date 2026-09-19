# COMMANDCLOCK cadence must be driven by the Clockwork runtime itself, not by
# the EtherCAT/IO sample path. cw has no EtherCAT transport (global_clock stays
# 0 and machine_is_ready never latches), so a clock that is only dispatched from
# handle_io_sampling never ticks there.
#
# Pass: the dependant's calcAdjust handler runs at least once ("calcAdjust tick").
# Fail: no tick ever arrives and no marker is logged.

COMMANDCLOCK MACHINE Guard {
  OPTION notify_period 100;
  OPTION command "calcAdjust";

  # State is observable; LOCAL keeps it out of dependant/channel noise.
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

COMMANDCLOCKCONSUMER MACHINE Clock {
  OPTION ticks 0;

  idle DEFAULT;
  idle INITIAL;

  # Lists the clock as a parameter, so it is a dependant of the clock and the
  # clock's notify fanout sends this command.
  COMMAND calcAdjust {
    INC ticks;
    LOG "COMMANDCLOCK calcAdjust tick";
  }
}

clock_consumer COMMANDCLOCKCONSUMER clock;
