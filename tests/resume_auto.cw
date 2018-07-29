/* This tests the behaviour of a RESUME with respect to changes of state
   of related machines while disabled
 */

Auto MACHINE input {

  on WHEN input IS on;
  off DEFAULT;

  ENTER on { LOG "on"; }
  ENTER off { LOG "off"; }

}

input FLAG;
auto Auto input;

Tester MACHINE input, auto {

  error WHEN auto IS off AND input IS on AND TIMER > 100;
  waiting WHEN auto ENABLED;
  updating DEFAULT;

  ENTER updating {
    SET input TO on;
  }

  COMMAND resume {
    RESUME auto;
  }
}

