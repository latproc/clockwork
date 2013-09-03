Blinker MACHINE {

  on STATE;
  off STATE;

  ENTER on { WAIT 1000; SET SELF TO off; }
  ENTER off { WAIT 1000; SET SELF TO on; }

}
blinker Blinker;
