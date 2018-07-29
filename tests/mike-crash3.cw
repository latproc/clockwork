TESTSTATE MACHINE {
  idle INITIAL;
  run STATE;
}

TESTLOC MACHINE List {
  idle INITIAL;
  hold STATE;
}

TESTREFCOPY MACHINE zones {
  State TESTSTATE;

  refWork REFERENCE;
  L_Current LIST;

  L_FrontShallow LIST; L_FrontDeep LIST;

  M_FrontShallow TESTLOC L_FrontShallow;
  M_FrontDeep TESTLOC L_FrontDeep;
  L_Stages LIST M_FrontShallow, M_FrontDeep;
  L_Steps LIST;

  done WHEN SELF IS done && L_Steps IS empty;
  done WHEN SELF IS Waiting && L_Steps IS empty && refWork IS NOT ASSIGNED;
  next WHEN SELF IS next && refWork IS ASSIGNED;
  next WHEN SELF IS Waiting && refWork IS ASSIGNED;
  Waiting WHEN State IS run;
  idle DEFAULT;

  COMMAND doNext { CLEAR refWork; }
  COMMAND start {
    CLEAR L_Steps;
    COPY ALL FROM L_Stages TO L_Steps;

    COPY ALL FROM zones TO L_FrontShallow WHERE zones.ITEM.type == 0;
    COPY ALL FROM zones TO L_FrontDeep WHERE zones.ITEM.type == 1;

    SET State TO run;
  }

  ENTER Waiting {
    CLEAR refWork; CLEAR L_Current;
    IF (L_Steps IS NOT empty) {
      x := TAKE FIRST FROM L_Steps;
      ASSIGN x TO refWork;
      COPY ALL FROM refWork.ITEM.List TO L_Current;
    };
  }

}
zone1 FLAG (type:0);
zone2 FLAG (type:0);
zone3 FLAG (type:1);
zone4 FLAG (type:1);
zones LIST zone1, zone2, zone3, zone4;

testRefCopy TESTREFCOPY zones;
