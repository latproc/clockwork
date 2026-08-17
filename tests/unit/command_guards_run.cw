# Standalone harness for command_guards.cw
#   cw command_guards.cw command_guards_run.cw

CommandGuardsHarness MACHINE driver {
  passed WHEN driver IS ok;
  failed WHEN driver IS error;
  idle DEFAULT;

  ENTER INIT { SEND run TO driver; }
  ENTER passed {
    LOG "command_guards: all cases PASSED";
    SHUTDOWN;
  }
  ENTER failed {
    LOG "command_guards: FAILED";
    SHUTDOWN;
  }
}

command_guards_harness CommandGuardsHarness test_command_guards_driver;
