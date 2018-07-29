SETSTATE MACHINE {
  idle INITIAL;
  run STATE;

}

TEST_REF_COPY MACHINE {
  State TESTSTATE;
  zone1 FLAG;
  zone2 FLAG;
  zone3 FLAG;
  zone4 FLAG;

  L_FrontShallow LIST zone1, zone2; L_FrontDeep LIST zone3, zone4;
  M_FrontShallow GRABZONEHASWORK (CarriageLoc:1, grabType:0) L_FrontShallow;
  M_FrontDeep GRABZONEHASWORK (CarriageLoc:1, grabType:1) L_FrontDeep;
  L_Stages LIST M_FrontShallow, M_FrontDeep;

  done WHEN SELF IS State && refWork IS empty;
  next WHEN SELF IS State && refWork IS ASSIGNED;
  Waiting WHEN State IS run;

  idle DEFAULT;
  COMMAND doNext { CLEAR refWork; }
  COMMAND start { SET State TO run; }

  L_Current LIST;
  refWork REFERENCE;
  ENTER Waiting {
    CLEAR refWork; CLEAR L_Current;
    IF (L_Steps IS NOT empty) {
      x := TAKE FIRST FROM L_Steps;
      ASSIGN x TO refWork;
      COPY ALL FROM refWork.ITEM.L_Work TO L_Current;
    };
  }

}

testRefCopy TEST_REF_COPY;
