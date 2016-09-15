# test of an invalid call to a machine in the wrong state

Target MACHINE {  
	OPTION counter 0;
	OPTION numstate 0;
	idle STATE;
	safe STATE;
	unsafe INITIAL;  
	
	one STATE; two STATE; three STATE; 
	ENTER one { numstate := 1; WAIT 1000 }
	ENTER two { numstate := 2; WAIT 2000 }
	ENTER three { numstate := 3; WAIT 3000 }
	
	COMMAND run WITHIN safe	{ LOG SELF.NAME + " got run command"; }

	COMMAND idle { SET SELF TO idle }
	COMMAND badstate { SET SELF TO badstate; }
	COMMAND inc { INC counter BY 1; }
	COMMAND reset { SET SELF TO unsafe }
	
	COMMAND start { SET SELF TO one }
	
	TRANSITION one TO two ON next;
	TRANSITION two TO three ON next;
	TRANSITION ANY TO one ON reset;

}
target Target;

Master MACHINE other {
	OPTION steps 0;
	running WHEN SELF IS running;
	idle DEFAULT;

	running DURING run  { CALL run ON other; SET SELF TO idle; }
	COMMAND badcall { CALL nothing ON other; LOG "badcall completed" }
	COMMAND idle { CALL idle ON other;  }
	COMMAND reset { CALL reset ON other }
	COMMAND count { CALL inc ON other } 
	                                    
	COMMAND start { CALL start ON other; steps := 1; }
	COMMAND next { CALL next ON other; steps := steps + 1 }
}
master Master target;
	

