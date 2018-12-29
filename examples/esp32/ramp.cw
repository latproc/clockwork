# ramp - output an increasing analogue value up to a set point 
#  and then decrease the value back to a starting point

Ramp MACHINE clock, output {
  OPTION start 1000;
  OPTION end 30000;
  OPTION direction 0;
  OPTION step 800;
  OPTION VALUE 0;

  top WHEN VALUE >= end AND direction > 0;
  bottom WHEN VALUE <= start AND direction < 0;
  rising WHEN VALUE < end AND direction > 0;
  falling WHEN VALUE > start AND direction < 0;
  stopped DEFAULT;
  ENTER top { VALUE := end; direction := -1; }
  ENTER bottom { VALUE := start; direction := 1; }

  ENTER INIT { VALUE := start; direction := 1; }

  RECEIVE clock.on_enter { VALUE := VALUE + direction * step; }
}

