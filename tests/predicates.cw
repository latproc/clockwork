# Various expression exercises, particular for the new LIST machine

Sample MACHINE {
    a FLAG;
    b FLAG;
    list LIST a;

    ready WHEN list INCLUDES a AND list INCLUDES b;
    on WHEN list INCLUDES a OR list INCLUDES b;
    off DEFAULT;
    
    ENTER on { LOG "Sample machine turned on"; INCLUDE b IN list; }
}
sample Sample;


Test01 MACHINE {
    a FLAG;
    b FLAG;
    c FLAG;
    d FLAG;
    flags LIST a,b,c;
    
    true WHEN TRUE;
    false DEFAULT;
    
    ENTER INIT { 
        IF (TRUE) { LOG "TRUE ok"; };
        IF (FALSE) { LOG "FALSE returned true"; } 
        ELSE { LOG "FALSE ok"; };

        IF (flags INCLUDES a) { LOG "a is in flags. correct" }
		ELSE { LOG "a is not in flags, incorrect" };

        INCLUDE d IN flags;
        INCLUDE d IN flags;
        IF (flags INCLUDES d) { LOG "d is in flags. correct"; } 
        ELSE {LOG "d is not in flags. incorrect" };

        IF (ALL flags ARE off) { LOG "all flags are off. correct" } 
        ELSE { LOG "Not all flags are off. incorrect"; };

        IF (ANY flags ARE off) { LOG "some flags are off. correct"} 
        ELSE { LOG "No flags are off. incorrect"; };

        SET b TO on;
        IF (ALL flags ARE on) { LOG "all flags are on. incorrect" } 
        ELSE { LOG "Not all flags are on. correct"; };

        IF (ANY flags ARE on) { LOG "some flags are on. correct"} 
        ELSE { LOG "No flags are on. incorrect"; };

        n := COUNT on FROM flags;
        LOG "There are " + n + " flags on";
        n := COUNT off FROM flags;
        LOG "There are " + n + " flags off";
        
        val := BITSET FROM flags on;
        LOG "on flags have value " + val;
        
        CREATE test WITH COPY 2 FROM flags;
        LOG "copied " + test + " from " + flags;
        
        CREATE test WITH TAKE 2 FROM flags;
        LOG "took " + test + " from " + flags;
    }
}
test01 Test01;

Test02 MACHINE {
    true WHEN FALSE;
    false DEFAULT;
}
test02 Test02;

