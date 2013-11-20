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

PuzzleCell MACHINE { OPTION contents 0; found WHEN contents != 0; blank DEFAULT; }
PuzzleRow MACHINE {
    cell1 PuzzleCell;
    cell2 PuzzleCell;
    
    row LIST cell1, cell2;

    working WHEN SELF IS unsolved AND TIMER>10;
    unsolved WHEN ANY row ARE blank;
    solved WHEN ALL row ARE found;
    
    ENTER working {
        LOG "working";
        IF (cell1 IS blank) { cell1.contents := 1; }
        ELSE { IF (cell2 IS blank) { cell2.contents := 1; } }
    }
    
}
Puzzle MACHINE {
    row1 PuzzleRow;
    row2 PuzzleRow;
    rows LIST row1, row2;
    
    solved WHEN ALL rows ARE solved;
    unsolved DEFAULT;
    ENTER solved { LOG "Puzzle solved"; }
}
puzzle Puzzle;

gOne FLAG;
gTwo FLAG;
GlobalList LIST gOne, gTwo;

GTest MACHINE list {
#GLOBAL gOne, gTwo;
    ok WHEN COUNT on FROM list == 2;
    waiting DEFAULT;
}
gtest GTest GlobalList;

TransitionTest  MACHINE {
    a STATE;
    b STATE;
    c STATE;
    init INITIAL;
    ready STATE;

    COMMAND log { LOG "transitioning to working" }

    TRANSITION ANY TO ready USING log;
}
tt TransitionTest;

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

