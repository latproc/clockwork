# Mikes file

COREINDEXDRIVE MACHINE C_Forward, C_Back {
  OPTION PERSISTENT true;

  Forward STATE;
  Back STATE;
  ForwardStopped STATE;
  BackStopped STATE;
  Idle INITIAL;

  COMMAND DriveFront { SET SELF TO Forward; }
  COMMAND DriveBack { SET SELF TO Back; }
 
  TRANSITION Forward TO ForwardStopped ON Stop;
  TRANSITION Back TO BackStopped ON Stop;
  TRANSITION ForwardStopped TO Forward ON Start;
  TRANSITION BackStopped TO Back ON Start;

  ENTER Forward {
    SET C_Back TO off; 
    SET C_Forward TO on; 
  }
  
 ENTER Back {
    SET C_Forward TO on; 
    SET C_Back TO on; 
  }
  ENTER ForwardStopped { 
    SET C_Forward TO off;
  }
  ENTER BackStopped { 
    SET C_Back TO off;
  }

 ENTER Idle {
    SET C_Forward TO off;
    SET C_Back TO off;
 }

}
