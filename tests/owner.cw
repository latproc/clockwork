# test the use of OWNER

ResumeFix MACHINE owner {
  fixing WHEN OWNER != OWNER.SavedState;
  good DEFAULT;
  ENTER fixing { 
	LOG "machine " + OWNER.NAME + " needs fix";
	SET OWNER TO PROPERTY OWNER.SavedState; 
  }

  ENTER good { LOG "machine " + OWNER.NAME + " seems good" }
}

# this machine starts up in the off state but should be moved 
# to 'fixed' due to the fixer doing its thing
Test MACHINE {
  OPTION SavedState "fixed";
  fixer ResumeFix SELF;
  
  on STATE;
  fixed STATE;
  off INITIAL;

}

test Test;

