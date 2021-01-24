MODULES {
    Beckhoff_EK1814 (position:0);#6000 7000;  # 4 IN, 4 OUT 
    Beckhoff_EL2535 (position:1);
    Beckhoff_EL3164 (position:2);
}

L_Modules LIST Beckhoff_EK1814, Beckhoff_EL2535, Beckhoff_EL3164;

L_Points LIST EK1814_OUT_0, EK1814_OUT_1, EK1814_OUT_2, EK1814_OUT_3, 
  EK1814_IN_0, EK1814_IN_1, EK1814_IN_2, EK1814_IN_3,
  ain, ain_error, ain_overrange, ain_settings, ain_underrange,
  raw, pos, vel, acc, update, helper;


EK1814_OUT_0	POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 0;
EK1814_OUT_1	POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 1;
EK1814_OUT_2	POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 2;
EK1814_OUT_3	POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 3;
EK1814_IN_0	POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 4;
EK1814_IN_1	POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 5;
EK1814_IN_2	POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 6;
EK1814_IN_3	POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 7;

SETUP MACHINE modules, points {
  GLOBAL ETHERCAT;
  GLOBAL L_Modules;

  error WHEN SELF IS error;
  op WHEN ALL modules ARE OP;
  error WHEN SELF IS op;
  preop WHEN (SELF IS startup || SELF IS preop) AND ALL modules ARE PREOP AND ETHERCAT IS CONFIG;
  startup DEFAULT;
  startup INITIAL;

  ENTER op { ENABLE modules; ENABLE pPoints; }

  ENTER error { LOG "ERROR: A module left the OP state"; }

  ENTER preop { SEND activate TO ETHERCAT; }

  ENTER startup {
    SYSTEM.CYCLE_DELAY := 2000;
    ETHERCAT.tolerance := 250;
    ENABLE ETHERCAT;
  }
}

STARTUP MACHINE modules, points {
  GLOBAL SYSTEM, ETHERCAT;
  LOCAL OPTION message "";
  OPTION hidden "message";

  preop_timeout WHEN SELF IS preop && TIMER >= 18000;
  op WHEN ALL modules ARE OP;
  preop WHEN SELF IS startup || SELF IS preop;
  startup DEFAULT;
  startup INITIAL;

  ENTER preop_timeout { SHUTDOWN; }
  ENTER op {
    ENABLE points;
  }
  ENTER preop { 
    LOG "activating ethercat"; 
    SEND activate TO ETHERCAT;
  }
  ENTER startup {
    ENABLE SYSTEM;
    SYSTEM.CYCLE_DELAY := 2000;
    ETHERCAT.tolerance := 150;
    ENABLE ETHERCAT;
    ENABLE modules;
  }
}

startup STARTUP L_Modules, L_Points;
Value MACHINE {
  EXPORT READONLY 16BIT VALUE;
  OPTION VALUE 0;
}
time Value;
raw Value;
pos Value;
vel Value;
acc Value;
stddev Value;

Update MACHINE raw_input, smoothed_input, vel_input, acc_input, stddev_input {
  LOCAL OPTION delay 100;
  update WHEN SELF IS idle AND TIMER >= delay;
  idle DEFAULT;

  ENTER update { 
    time.VALUE := ain.IOTIME; 
    raw_input.VALUE := ain.raw; 
    smoothed_input.VALUE := 0.9 * ain.VALUE; 
    vel_input.VALUE := ain.Velocity; 
    acc_input.VALUE := ain.Acceleration; 
    stddev_input.VALUE := ain.stddev; 
  }
}
update Update raw, pos, vel, acc, stddev;
