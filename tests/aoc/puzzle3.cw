# counting common bits
#

# Listen for character data arriving on a network port,
DeviceConnector CHANNEL {
  OPTION port 7711;
  PUBLISHER;
}

# An input queue, status indicates whether the device connection
# is available or not.
Data MACHINE {
  OPTION status "";
  idle INITIAL;
  queue LIST;
}

# collect lines of 0|1 and push each line onto an output queue.
Splitter MACHINE input, out {
  OPTION buf "";
  collecting WHEN SELF IS idle AND input.queue IS NOT empty;
  splitting WHEN SELF IS idle AND buf MATCHES `[01]*.`;
  idle DEFAULT;

  # collect raw data as it arrives
  ENTER collecting {
    x := TAKE FIRST FROM input.queue;
    buf := buf + x;
  }
  # push line data
  ENTER splitting {
    x := EXTRACT `([01]*).` FROM buf;
    PUSH x TO out;
  }
}

data Data;
log LIST;
splitter Splitter data, log;

# Clockwork doesn't have dynamic allocation
# and the GENERATE statement isn't done yet
f31 FlipCounter(value: 2147483647, bit: 31);
f30 FlipCounter(value: 1073741824, bit: 30);
f29 FlipCounter(value: 536870912, bit: 29);
f28 FlipCounter(value: 268435456, bit: 28);
f27 FlipCounter(value: 134217728, bit: 27);
f26 FlipCounter(value: 67108864, bit: 26);
f25 FlipCounter(value: 33554432, bit: 25);
f24 FlipCounter(value: 16777216, bit: 24);
f23 FlipCounter(value: 8388608, bit: 23);
f22 FlipCounter(value: 4194304, bit: 22);
f21 FlipCounter(value: 2097152, bit: 21);
f20 FlipCounter(value: 1048576, bit: 20);
f19 FlipCounter(value: 524288, bit: 19);
f18 FlipCounter(value: 262144, bit: 18);
f17 FlipCounter(value: 131072, bit: 17);
f16 FlipCounter(value: 65536, bit: 16);
f15 FlipCounter(value: 32768, bit: 15);
f14 FlipCounter(value: 16384, bit: 14);
f13 FlipCounter(value: 8192, bit: 13);
f12 FlipCounter(value: 4096, bit: 12);
f11 FlipCounter(value: 2048, bit: 11);
f10 FlipCounter(value: 1024, bit: 10);
f9 FlipCounter(value: 512, bit: 9);
f8 FlipCounter(value: 256, bit: 8);
f7 FlipCounter(value: 128, bit: 7);
f6 FlipCounter(value: 64, bit: 6);
f5 FlipCounter(value: 32, bit: 5);
f4 FlipCounter(value: 16, bit: 4);
f3 FlipCounter(value: 8, bit: 3);
f2 FlipCounter(value: 4, bit: 2);
f1 FlipCounter(value: 2, bit: 1);
f0 FlipCounter(value: 1, bit: 0);
counters LIST f31,f30,f29,f28,f27,f26,f25,f24,f23,f22,f21,f20,f19,f18,f17,f16,f15,f14,f13,f12,f11,f10,f9,f8,f7,f6,f5,f4,f3,f2,f1,f0;


analyzer Analyzer log, counters;

# A FlipCounter can be turned on but it is unstable
# and automatically turns off again.
# A count of the number of flips is maintained until reset.
FlipCounter MACHINE {
  OPTION count 0;
  on STATE;
  off DEFAULT;
  ENTER on { INC count; }
  COMMAND reset { count := 0; }
}

# Automatically convert a binary string into a decimal number.
# Syncronise by watching the busy and done states.
BinaryToDecimal MACHINE {
  OPTION binary "";
  OPTION decimal 0;
  OPTION num_bits 0;
  working FLAG;
  completed FLAG;

  done WHEN SELF IS done || SELF IS busy AND binary == "";
  busy WHEN SELF IS busy_checking && binary != "";
  busy_checking WHEN binary != "";
  idle DEFAULT;
  ENTER busy {
    INC num_bits;
    SET working TO on;
    x := EXTRACT `(.)` FROM binary;
    x := x AS INTEGER;
    decimal := decimal * 2 + x;
  }
  ENTER done { SET working TO off; }
  COMMAND reset WITHIN done { decimal := 0; num_bits := 0; SET SELF TO idle; }
}

# Manually create a BITSET from the counters that meet a threshold
GammaRateCalculator MACHINE counters {
  OPTION threshold 0;
  OPTION max_bit 0;
  idle DEFAULT;
  bits LIST;

  COMMAND calculate {
    CLEAR bits;
    COPY ALL FROM counters TO bits WHERE counters.ITEM.count >= threshold;
    result := SUM value FROM bits;
  }

}

# Manually create a BITSET from the counters that do not exceed a threshold
EpsilonRateCalculator MACHINE counters {
  OPTION threshold 0;
  idle DEFAULT;
  bits LIST;

  COMMAND calculate {
    CLEAR bits;
    COPY ALL FROM counters TO bits WHERE counters.ITEM.count <= threshold && counters.ITEM.bit <= max_bit;
    result := SUM value FROM bits;
  }

}

Analyzer MACHINE input, flags {
  OPTION power_consumption 0;
  OPTION gamma_rate 0;
  OPTION epsilon_rate 0;
  OPTION count 0;

  converter BinaryToDecimal;
  gamma_rate_calculator GammaRateCalculator flags;
  epsilon_rate_calculator EpsilonRateCalculator flags;

  waiting WHEN converter IS working;
  analyzing WHEN converter IS done;
  loading WHEN SELF IS idle AND converter IS idle AND input IS NOT empty;
  idle DEFAULT;

  ENTER loading {
    INC count;
    x := TAKE FIRST FROM input;
    converter.binary := x;
    WAITFOR converter IS busy;
  }

  ENTER analyzing {
    SET ENTRIES OF flags FROM BITSET converter.decimal;
    epsilon_rate_calculator.max_bit := converter.num_bits - 1;
    SEND reset TO converter;
  }

  COMMAND calculate {
    gamma_rate_calculator.threshold := count / 2;
    CALL calculate ON gamma_rate_calculator;
    gamma_rate := gamma_rate_calculator.result;

    epsilon_rate_calculator.threshold := count / 2;
    CALL calculate ON epsilon_rate_calculator;
    epsilon_rate := epsilon_rate_calculator.result;

    power_consumption := gamma_rate * epsilon_rate;
  }

}

