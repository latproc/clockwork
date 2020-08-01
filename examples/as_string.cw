# play with AS STRING

ast AsStringTest;
AsStringTest MACHINE {
	OPTION a 3;
  OPTION b 5.324;

  COMMAND test {
    x := FORMAT a WITH "%d";
    LOG "a: " + FORMAT a WITH "%04d";
    LOG "b: " + (FORMAT b WITH "%4.1f");
  }
}
