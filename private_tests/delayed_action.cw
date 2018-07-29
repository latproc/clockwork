Delayer MACHINE input, output {
    on WHEN SELF IS on, EXECUTE flipOff WHEN TIMER>=50;
    off WHEN SELF IS off, EXECUTE flipOn WHEN TIMER>=50;
    RECEIVE input.on_enter { SET SELF TO off; }
    RECEIVE input.off_enter {  SET SELF TO on;}
    COMMAND flipOn { SET output TO on; }
    COMMAND flipOff { SET output TO off; }
}

SIMDELAYEDACTION MACHINE(tab:GrabSimulate) input{
    on STATE;
    off INITIAL;
    delayer Delayer input, OWNER;
}

out FLAG;
in FLAG;

# two alternative ways to implement the delay
dout SIMDELAYEDACTION in;
op Delayer in, out;
