# A small test rig.
MODULES {
    Beckhoff_EK1814 (position:0);#6000 7000;  # 4 IN, 4 OUT 
    Beckhoff_EL2535 (position:1,
      config_file:"/home/martin/projects/clockwork/tests/xml/Beckhoff EL25xx.xml",
      alternate_sync_manager:"Extended info data", ProductCode:0x09e73052, RevisionNo:0x00150000);
    Beckhoff_EL3164 (position:2);
    Beckhoff_EL5152 (position:3,
      config_file:"/home/martin/projects/clockwork/tests/xml/Wooltech_EL5152.xml",
      alternate_sync_manager:"Wooltech 32 Bit (MDP 511)", ProductCode:0x14203052, RevisionNo:0x00120000);
    Beckhoff_EL2535_2 (position:4,
      config_file:"/home/martin/projects/clockwork/tests/xml/Beckhoff EL25xx.xml",
      alternate_sync_manager:"Extended info data", ProductCode:0x09e73052, RevisionNo:0x00120000);
}

L_Modules LIST Beckhoff_EK1814, Beckhoff_EL2535, Beckhoff_EL3164,
    Beckhoff_EL5152, Beckhoff_EL2535_2;

EK1814_OUT_0 POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 0;
EK1814_OUT_1 POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 1;
EK1814_OUT_2 POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 2;
EK1814_OUT_3 POINT (type:Output, tab:Outputs, image:output64x64) Beckhoff_EK1814, 3;
EK1814_IN_0  POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 4;
EK1814_IN_1  POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 5;
EK1814_IN_2  POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 6;
EK1814_IN_3  POINT (type:Input, tab:Inputs, image:input64x64) Beckhoff_EK1814, 7;

# Define a machine called 'STARTUP' to take control of the startup sequence.
STARTUP MACHINE modules, points {
  GLOBAL ETHERCAT;
  GLOBAL L_Modules;

  error WHEN SELF IS error;
  op WHEN ALL modules ARE OP;
  error WHEN SELF IS op;
  preop WHEN (SELF IS startup || SELF IS preop) AND ALL modules ARE PREOP AND ETHERCAT IS CONFIG;
  startup DEFAULT;
  startup INITIAL;

  ENTER op { }

  ENTER error { LOG "ERROR: A module left the OP state"; }

  ENTER preop { SEND activate TO ETHERCAT; }

  ENTER startup {
    SYSTEM.CYCLE_DELAY := 2000;
    ETHERCAT.tolerance := 250;
    ENABLE ETHERCAT;
    ENABLE modules;
    ENABLE points;
  }
}

startup STARTUP L_Modules, L_Points;

# Messing around with analogue inputs.
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

L_Points LIST EK1814_OUT_0, EK1814_OUT_1, EK1814_OUT_2, EK1814_OUT_3, 
  EK1814_IN_0, EK1814_IN_1, EK1814_IN_2, EK1814_IN_3,
  ain, ain_error, ain_overrange, ain_settings, ain_underrange,
  raw, pos, vel, acc, update, helper;

ANALOGIN_SETTINGS MACHINE { 
# a third-order pass-through filter that doesn't cutoff any frequencies
#  C LIST 1.0,3.0,3.0,1.0;
#  D LIST 1.0,3.0,3.0,1.0;

#C LIST 0.000061006178758, 0.000122012357516, 0.000061006178758;
#D LIST 1.000000000000, -1.977786483777, 0.978030508492;

# a third order low pass filter that cuts of at 0.01*Pi
#  C LIST 0.000003756838020,0.000011270514059,0.000011270514059,0.000003756838020;
#  D LIST 1.000000000000,-2.937170728450,2.876299723479,-0.939098940325;

# third order low pass filter 0.1
  C LIST 0.002898194633721,0.008694583901164,0.008694583901164,0.002898194633721;
  D LIST 1.000000000000,-2.374094743709,1.929355669091,-0.532075368312;

  OPTION enable_velocity 1;
  OPTION enable_acceleration 1;
  OPTION enable_stddev 1;

  OPTION velocity_scale 1.0;
  OPTION acceleration_scale 1.00;
  OPTION throttle 30;
 
  idle INITIAL;
}

ain_settings ANALOGIN_SETTINGS(filter:2);
ain_underrange POINT(export:ro) Beckhoff_EL3164, 0;
ain_overrange POINT(export:ro) Beckhoff_EL3164, 1;
ain_error POINT(export:ro) Beckhoff_EL3164, 4;
ain ANALOGINPUT Beckhoff_EL3164, 10, ain_settings;

helper SETTINGS_HELPER ain, ain_settings;

# this machine helps update the filter settings
SETTINGS_HELPER MACHINE input, settings {

  OPTION c0 0.000003756838020;
  OPTION c1 0.000011270514059;
  OPTION C2 0.000011270514059;
  OPTION c3 0.000003756838020;

  OPTION d0 1.000000000000;
  OPTION d1 -2.937170728450;
  OPTION d2 2.876299723479;
  OPTION d3 -0.939098940325;

  COMMAND update {
    CLEAR settings.C;
    PUSH c0 TO settings.C;
    PUSH c1 TO settings.C;
    PUSH c2 TO settings.C;
    PUSH c3 TO settings.C;
    CLEAR settings.D;
    PUSH d0 TO settings.C;
    PUSH d1 TO settings.C;
    PUSH d2 TO settings.C;
    PUSH d3 TO settings.C;

    DISABLE input;
    WAIT 100;
    ENABLE input;
  }
}

# TODO: Access some of the analogue input status values.
#Status__Underrange
#Status__Overrange
#Status__Limit1
#Status__Limit2
#Status__Error
#Status__Sync error
#Status__TxPDO State
#Status__TxPDO Toggle
#AI Standard Channel 1 Value

# Poll analogue inputs on a regulare basis.
Update MACHINE raw_input, smoothed_input, vel_input, acc_input, stddev_input, time_input {
  LOCAL OPTION delay 100;
  update WHEN SELF IS idle AND TIMER >= delay;
  idle DEFAULT;

  ENTER update { 
    time_input.VALUE := ain.IOTIME; 
    raw_input.VALUE := ain.raw; 
    smoothed_input.VALUE := 0.9 * ain.VALUE; 
    vel_input.VALUE := ain.Velocity; 
    acc_input.VALUE := ain.Acceleration; 
    stddev_input.VALUE := ain.stddev; 
  }
}
update Update raw, pos, vel, acc, stddev, time;
