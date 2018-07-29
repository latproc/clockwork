SETTINGS MACHINE { OPTION rate 30; }

PINGGENERATOR MACHINE Settings, List {
  OPTION DELAY 0;
  on WHEN SELF IS off AND TIMER >= Settings.rate || SELF IS on AND TIMER < Settings.rate;
  off DEFAULT;
  ENTER on { SEND calcAdjust TO List; }
  ENTER off { SEND calcAdjust TO List; }
  ENTER INIT { WAIT DELAY; }
}
ping PINGGENERATOR settings, clocks;

TESTCLOCK MACHINE Settings, List {
	LOCAL OPTION next 0, last 0;
	LOCAL OPTION wait_time 0;
  
  update WHEN SELF IS idle && TIMER >= wait_time;
  idle STATE;              
  inactive INITIAL;
  ENTER idle { wait_time := last + Settings.rate - NOW; }
  LEAVE idle { last := NOW; SEND calcAdjust TO List; }
  ENTER update { SET SELF TO idle; }
}
Flip MACHINE { 
	on STATE; off STATE; off INITIAL; 
	TRANSITION on TO off ON calcAdjust;
	TRANSITION off TO on ON calcAdjust;
}

flip Flip;
clocks LIST flip;
settings SETTINGS;
clock TESTCLOCK settings, flip;
