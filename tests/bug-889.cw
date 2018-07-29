# Unreproducible issue
#
# odd messages were seen in the log regarding this machine:
# Obj_BaleEntryLoader.Cleared: no such property or machine: DEFAULT
# Cleared condition failed: predicate failed to resolve: (DEFAULT (true) OR DEFAULT)

PFLAG MACHINE {
OPTION PERSISTENT true;
OPTION export rw;
OPTION State "off";

on WHEN State IS "on";
  off DEFAULT;
  off INITIAL;
COMMAND turnOn {
    State := "on";
  }
COMMAND turnOff {
    State := "off";
  }
TRANSITION off TO on ON turnOn;
  TRANSITION on TO off ON turnOff;
}

test_pflag PFLAG;
