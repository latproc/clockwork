
dump Dump (tab:Test) actions, dump_output;

# simulated outputs and actions
dump_output FLAG(tab:Test);
out1 FLAG(tab:Test); 
out2 FLAG(tab:Test); 
out3 FLAG(tab:Test);

a DumpAction(tab:Test, mask:1) actions, out1, dump;
b DumpAction(tab:Test, mask:2) actions, out2, dump;
c DumpAction(tab:Test, mask:4) actions, out3, dump;

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


DumpActionInternal MACHINE actions, dump {
    OPTION mask 0;
    on WHEN actions.VALUE & mask == mask && dump IS on AND dump.TIMER >= 5;
    off DEFAULT;
    ENTER unknown { LOG " unknown state!"}
}

DumpAction MACHINE actions, output, dump {
    OPTION mask 0;
    action_internal DumpActionInternal actions, dump;
    
    on  STATE;
    off STATE;
    
    ENTER on { SET output TO on; }
    ENTER off { SET output TO off; }
    
    COMMAND TurnOn { 
        LOG "turnOn";
        actions.VALUE := actions.VALUE | mask; 
        WAITFOR action_internal IS on;
    }
    COMMAND TurnOff { 
        LOG "turnOff";
        actions.VALUE := actions.VALUE & (mask ^ -1); 
        WAITFOR action_internal IS off;
    }
    TRANSITION on TO off USING TurnOff;
    TRANSITION off TO on USING TurnOn;
    
    ENTER INIT { action_internal.mask := mask; SET SELF TO off; }
}

