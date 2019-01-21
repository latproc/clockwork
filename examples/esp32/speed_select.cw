# this monitors a button and toggles the pulse speed when pressed

SpeedSelect MACHINE button, pulser {
  fast WHEN pulser.delay < 200;
  slow DEFAULT;

  RECEIVE button.off_enter {
    SEND toggle_speed TO pulser;
  }
}

DebouncedInput MACHINE in {
  OPTION debounce_time 100;
  OPTION off_time 50;
  off WHEN in IS off AND in.TIMER >= debounce_time || SELF IS off && TIMER < off_time;
  on DEFAULT;

  ENTER on { LOG "Debounced input on"; }
  ENTER off { LOG "Debounce input off"; }
}

