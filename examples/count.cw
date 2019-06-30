# tests for the COUNT operation

# send run to the script to start the tests
count_test_script CountTestScript test_count_setup_within_init;

CountTestScript MACHINE test_init_setup {
	OPTION step 0;

  ok WHEN SELF IS ok;
  idle DEFAULT;
  working DURING run {
    WAITFOR test_init_setup IS ok;
    # add other tests here
    SET SELF TO ok;
  }
}

# the following verifies that a list can be setup manually during
# INIT and produce a correct result on the first stable state test
TestCountSetupWithinInit MACHINE {
  a FLAG;
  b FLAG;
  c FLAG;
  flags LIST;

  ok WHEN COUNT on FROM flags == 1;
  error WHEN SELF IS error;
  error DEFAULT;

  ENTER INIT {
    SET a TO on;
    PUSH a TO flags;
    PUSH b TO flags;
    PUSH c TO flags;
  }
}
test_count_setup_within_init TestCountSetupWithinInit;

