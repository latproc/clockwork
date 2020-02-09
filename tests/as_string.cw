# play with AS STRING

ast AsStringTest;
AsStringTest MACHINE {
	OPTION a 3;
  OPTION b 5.324;
  OPTION c "";

  COMMAND concat {
    c := "A: " + "01";
  }

  COMMAND test {
    x := FORMAT a WITH "%04d";
    sec := 5;
    sec := FORMAT sec WITH "%02d";
	  LOG sec;
    LOG "a: " + FORMAT a WITH "%04d";
    LOG "b: " + (FORMAT b WITH "%4.1f");
  }
}
