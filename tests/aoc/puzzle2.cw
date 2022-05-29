# track movement
#

DeviceConnector CHANNEL {
  OPTION port 7711;
  PUBLISHER;
}

Data MACHINE {
  OPTION status "";
  idle INITIAL;
  queue LIST;
}

Splitter MACHINE input, out {
  OPTION buf "";
  collecting WHEN SELF IS idle AND input.queue IS NOT empty;
  splitting WHEN SELF IS idle AND buf MATCHES `[a-z][a-z]* [0-9]*[^0-9].*`;
  idle DEFAULT;

  ENTER collecting {
    x := TAKE FIRST FROM input.queue;
    buf := buf + x;
  }
  ENTER splitting {
    x := EXTRACT `([a-z]* [0-9]*).` FROM buf;
    PUSH x TO out;
  }
}

data Data;
movements LIST;
splitter Splitter data, movements;
pilot Pilot movements;

Pilot MACHINE input {
  OPTION direction "";
  OPTION position 0;
  OPTION depth 0;

  OPTION window_size 1;
  window LIST;

  up WHEN SELF IS busy AND direction == 'up';
  down WHEN SELF IS busy AND direction == 'down';
  forward WHEN SELF IS busy AND direction == 'forward';

  ENTER up { depth := depth - distance; }
  ENTER down { depth := depth + distance; }
  ENTER forward { position := position + distance; }

  loading WHEN input IS NOT empty AND SIZE OF window < window_size;
  busy WHEN SELF IS idle AND SIZE OF window == window_size;
  idle DEFAULT;

  ENTER loading { x := TAKE FIRST FROM input; PUSH x TO window; }

  COMMAND next {
    x := TAKE FIRST FROM window;
    direction := EXTRACT `([a-z]*).` FROM x;
    distance := EXTRACT `([0-9]*)` FROM x;
    distance := distance AS INTEGER;
    LOG direction + ": " + distance;
  }

  ENTER busy { CALL next; }

  COMMAND report { LOG "distance: " + position + " depth: " + depth + " area: " + (position * depth); }
}


