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
    LOG "paused";
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
    LOCAL OPTION last_tick 0;
    OPTION dt 0;
    idle LOCAL STATE;
    idle DEFAULT;
    tick WHEN TIMER >= step;
    ENTER tick {
        dt := NOW - last_tick;
        last_tick := NOW;
    }
    LEAVE tick {
        IF (TIMER >10) {
          LOG "slow to change state " + TIMER;
        }
    }
}
