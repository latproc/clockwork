# Examples of the RECEIVES statement.

clock Clock;
rising_edge RisingEdge clock;
falling_edge FallingEdge clock;

RisingEdge MACHINE clock {
  OPTION last 0;
  OPTION delta 0;
  LOCAL OPTION t 0;
  idle INITIAL;
  paused STATE;

  # Measure time delta when idle
  RECEIVE clock.tick_enter WITHIN idle {
    t := NOW;
    delta := t - last;
    last := t;
  }

  # Do nothing when paused
  RECEIVE clock.tick_enter WITHIN paused {
  }
}

FallingEdge MACHINE clock {
  OPTION last 0;
  OPTION delta 0;
  LOCAL OPTION t 0;

  RECEIVE clock.tick_leave {
    t := NOW;
    delta := t - last;
    last := t;
  }
}

Clock MACHINE {
    OPTION step 500;
    idle DEFAULT;
    tick WHEN TIMER >= step;
}
