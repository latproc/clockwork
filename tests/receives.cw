# Examples of the RECEIVES statement.

clock Clock;
rising_edge RisingEdge clock;

RisingEdge MACHINE clock {
  OPTION last 0;
  OPTION delta 0;
  LOCAL OPTION t 0;
  idle INITIAL;
  paused STATE;

  RECEIVE clock.tick_enter WITHIN idle {
    t := CLOCK;
    delta := t - last;
    last := t;
  }

  RECEIVE clock.tick_enter WITHIN paused {
  }
}

Clock MACHINE {
    OPTION step 500;
    idle DEFAULT;
    tick WHEN TIMER >= step;
}
