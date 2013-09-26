# This file examines the


Counter MACHINE x, started, disabled {
    OPTION count 0;
    stepping WHEN SELF IS active AND TIMER > 500;
    active WHEN started IS on AND disabled IS off;
    off DEFAULT;
    
    ENTER stepping { count := count + 1; }
    COMMAND reset { count := 0; SET started TO off; }

}

Main MACHINE started, disabled{

    running FLAG(tab:tests);
 	dummy FLAG(tab:tests); 
    counter Counter(tab:tests) dummy,started, disabled;

    COMMAND start  { SET running TO on }
    COMMAND stop { SET running TO off }
    
}

m Main(tab:tests) b, c;
a FLAG(tab:tests);
b FLAG(tab:tests);
c FLAG(tab:tests);

