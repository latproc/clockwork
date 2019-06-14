/* A test of enabling and disabling a list of machines

  The enable/disable process should be done in order of dependency
*/

b1 Basic;
b2 Basic;
b3 Basic;

a1 Controller b2,b3;
a2 Controller b3,b1;

all LIST a1,a2,b1,b2,b3,enable_test,disable_test,subject;

m1 Master all; 
startup STARTUP m1;

enable_test EnableTest subject;
disable_test DisableTest subject;
subject FLAG;

Basic MACHINE { 
	Idle DEFAULT;
}

Controller MACHINE a,b {
	Idle DEFAULT;
}

Master MACHINE machines {
	Idle DEFAULT;
  ENTER INIT { ENABLE machines; }
}

EnableTest MACHINE other {
  error WHEN SELF IS error;
	ok WHEN other ENABLED;
  waiting DEFAULT;
  ENTER ok {
    IF (other DISABLED) {
      LOG other.NAME + " should be disabled";
      SET SELF TO error;
    }
  }
}


DisableTest MACHINE other {
  error WHEN SELF IS error;
	ok WHEN other DISABLED;
  waiting DEFAULT;

  ENTER ok {
    IF (other ENABLED) {
      LOG other.NAME + " should be disabled";
      SET SELF TO error;
    }
  }
}

STARTUP MACHINE machines {
	COMMAND start { ENABLE machines; }
	COMMAND stop { DISABLE machines; }
}
