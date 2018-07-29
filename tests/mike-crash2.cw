TESTSTATE MACHINE {
  idle INITIAL;
  run STATE;
}
Dummy MACHINE list { 
	L_Work LIST;
	ENTER INIT { COPY ALL FROM list TO L_Work; }
}

TESTREFCOPY MACHINE zones {
  State TESTSTATE;

  refWork REFERENCE;
  L_Current LIST;
  L_FrontShallow LIST zone1, zone2; L_FrontDeep LIST zone3, zone4;
  M_FrontShallow Dummy L_FrontShallow;
  M_FrontDeep Dummy L_FrontDeep;
  L_Stages LIST M_FrontShallow, M_FrontDeep;
  L_Steps LIST;

  done WHEN SELF IS Waiting && L_Steps IS empty;
  next WHEN SELF IS Waiting && refWork IS ASSIGNED;
  Waiting WHEN State IS run;
  idle DEFAULT;

  COMMAND doNext { CLEAR refWork; }
  COMMAND start {
    COPY ALL FROM zones TO L_FrontShallow WHERE zones.ITEM.type == 0;
    COPY ALL FROM zones TO L_FrontDeep WHERE zones.ITEM.type == 1;

    COPY ALL FROM L_Stages TO L_Steps;
    SET State TO run;
  }

  ENTER Waiting {
    CLEAR refWork; CLEAR L_Current;
    IF (L_Steps IS NOT empty) {
      x := TAKE FIRST FROM L_Steps;
      ASSIGN x TO refWork;
      COPY ALL FROM refWork.ITEM.L_Work TO L_Current;
    };
  }

}
zone1 FLAG (type:0);
zone2 FLAG (type:0);
zone3 FLAG (type:1);
zone4 FLAG (type:1);
zones LIST zone1, zone2, zone3, zone4;

testRefCopy TESTREFCOPY zones;
