# this Monitor machine does not yet work. It is a placeholder for future functionality

#Monitor MACHINE machine {
#  RECEIVE ALL AS evt FROM machine {
#    LOG "Received " + evt + " from " + machine.NAME TO globals.logfilename;
#  };
#}


ConditionDisplay MACHINE {
    one WHEN SELF IS waiting AND TIMER > 100;
    waiting DEFAULT;
}
condition_displat ConditionDisplay; 
