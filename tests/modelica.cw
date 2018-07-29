
master Master;

Master MACHINE {
	OPTION v 0;
	LOCAL OPTION count 0;
	LOCAL OPTION last "state1";
	state1 WHEN SELF IS state2 && v >= 20;
	state2 WHEN last == "state2" || (one IS stateD && two IS stateY), EXECUTE tick WHEN TIMER > 1000;
	state1 WHEN last == "state1" || (SELF IS state2 && v >= 20), EXECUTE tick WHEN TIMER > 1000;
	one SubOne SELF;
	two SubTwo OWNER;
	ENTER state1 { last := "state1" } 
	ENTER state2 { last := "state2"; v := v + 5 }
	ticking DURING tick {  }
	ENTER INIT { SET SELF TO state1; }
}

SubOne MACHINE owner { 
	LOCAL OPTION last "stateA";
	inactive WHEN OWNER IS NOT state1 AND OWNER IS NOT ticking; 
	
	stateD WHEN (last == "stateD" || SELF IS stateD) || (SELF IS stateC AND OWNER.count >= 2);
	ENTER stateD { last := "stateD" }

	stateC WHEN (last == "stateC" || SELF IS stateC) || (SELF IS stateB AND OWNER.v == 0);
	ENTER stateC { last := "stateC"; OWNER.count := OWNER.count + 1 }

	stateB WHEN (last == "stateB") || (SELF IS stateA &&  OWNER.v >= 6);
	ENTER stateB { last := "stateB"; OWNER.v := OWNER.v - 1 }

	stateA WHEN (last == "stateA" || SELF IS stateA);
	ENTER stateA { OWNER.v := OWNER.v + 2 } 
	stateA DEFAULT;

	ENTER inactive { last := "stateA" }
	
	ticking STATE;
	RECEIVE OWNER.ticking_enter { SET SELF TO ticking }
	 
}

SubTwo MACHINE owner {
	LOCAL OPTION w 0;
	OPTION i 0;
	OPTION j 0;
	LOCAL OPTION last "stateX"; 
	
	inactive WHEN OWNER IS NOT state1 && OWNER IS NOT ticking; 
	stateY WHEN last == "stateY" || SELF IS stateX && i > 20;  ENTER stateY { last := "stateY"; j := j + 1 }
	stateX WHEN last == "stateX" || SELF IS stateX;  ENTER stateX { last := "stateX";  i := i + 1; w := OWNER.v }
	
	ENTER INIT { i := 0; j := 0;}

	ticking STATE;
	RECEIVE OWNER.ticking_enter { SET SELF TO ticking }
	LEAVE inactive { i := 0; j := 0; }

}
