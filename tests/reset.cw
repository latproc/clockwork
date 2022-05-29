# test the basic functionality of reset
#
# RESET disables and then reenables all machines

Sample MACHINE {
  ENTER INIT { LOG "initialised " + SELF.NAME; }
}

Resetter MACHINE {
  COMMAND reset { RESET; }
}

sample Sample;
resetter Resetter;
