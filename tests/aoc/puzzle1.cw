# Count the number of times a depth measurement increases from the previous measurement.
#
# Values are piped in from the device connector to a queue and processed one-by-one
# A report is logged each time the queue empties.

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
  splitting WHEN SELF IS idle AND buf MATCHES `[^0-9]*[0-9]*[^0-9].*`;
  idle DEFAULT;

  ENTER collecting {
    x := TAKE FIRST FROM input.queue;
    buf := buf + x;
  }
  ENTER splitting {
    x := EXTRACT `[^0-9]*([0-9]*).` FROM buf;
    x := x AS INTEGER;
    PUSH x TO out;
  }
}

data Data;
measurements LIST;
splitter Splitter data, measurements;
proc MeasurementProcessor measurements;

MeasurementProcessor MACHINE input {
  OPTION value 0;
  OPTION increments 0;
  OPTION count 0;

  increased WHEN SELF IS busy AND value > last;
  initialising WHEN count == 0 AND input IS NOT empty;
  busy WHEN SELF IS idle AND input IS NOT empty;
  idle DEFAULT;

  ENTER increased {
    increments := increments + 1;
    LOG "" + increments  + " increments in " + count + " values";
  }

  COMMAND next {
    count := count + 1;
    last := value;
    value := TAKE FIRST FROM input;
  }

  ENTER busy { CALL next; }
  ENTER initialising { CALL next; }

}


