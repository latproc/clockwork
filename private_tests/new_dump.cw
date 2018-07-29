
/*
    when an action becomes active with the dump off
       turn on the dump
       wait 5ms
       turn on the action
       
    when an action releases with no other actions on
       turn off the dump
       wait 5ms
       turn off the action
       
    when an action releases with other actions active
       turn off the action
    
    when an action becomes active with the dump on
       turn on the action

    if another action needs the dump while this action is turning it off
        ...
        
    if two actions want the dump off at the same time
        ...
        
*/

dump Dump (tab:Test) actions, dump_output;

# simulated outputs and actions
dump_output FLAG(tab:Test);
out1 FLAG(tab:Test); 
out2 FLAG(tab:Test); 
out3 FLAG(tab:Test);

a DumpAction(tab:Test, mask:1) actions, out1;
b DumpAction(tab:Test, mask:2) actions, out2;
c DumpAction(tab:Test, mask:4) actions, out3;

Actions MACHINE {
    OPTION VALUE 0; # bitset
}
actions Actions; 

Dump MACHINE actions, output {
    on WHEN actions.VALUE != 0;
    off DEFAULT;
    ENTER on { SET output TO on; }
    ENTER off { SET output TO off; }
}

DumpAction MACHINE actions, action {
    OPTION mask 0;
    
    COMMAND On { SET SELF TO on }
    COMMAND Off { SET SELF TO off }
    
    off WHEN actions.VALUE == 0;
    off_shared WHEN actions.VALUE & mask == 0;  # another action is using the dump
    on_shared WHEN actions.VALUE != mask;       # this and other actions are using the dump
    on WHEN actions.VALUE == mask;              # this action is the sole user of the dump

    TRANSITION off TO on USING turnOnWithDelay;
    TRANSITION on TO off USING turnOffWithDelay;
    TRANSITION off_shared TO on USING turnOnImmediately;
    TRANSITION on_shared TO off USING turnOffImmediately;
    
    COMMAND turnOnWithDelay  {
     LOG "turnOnWithDelay";
        CALL SetOn ON SELF; 
        WAIT 5;
        SET action TO on;
    }
    
    COMMAND turnOffWithDelay { 
     LOG "turnOffWithDelay";
        CALL SetOff ON SELF;
        WAIT 5;
        SET action TO off;
    }
    
    # controlling our output
    COMMAND turnOnImmediately { 
     LOG "turnOnImmediately";
        CALL SetOn ON SELF; 
        SET action TO on; 
    }
    COMMAND turnOffImmediately { 
     LOG "turnOffImmediately";
        CALL SetOff ON SELF;
        SET action TO off; 
    }

    # setting our flag bit to indicate we are using the dump
    RECEIVE SetOn { actions.VALUE := actions.VALUE | mask; }  
    RECEIVE SetOff { actions.VALUE := actions.VALUE & (mask ^ -1);}

    # Catching an error state (debug helper)
    unknown DEFAULT;
    error WHEN SELF IS unknown OR SELF IS error; # detecting an error state ('can't happen')
    ENTER error { LOG " Error (should not be possible)" }
}

