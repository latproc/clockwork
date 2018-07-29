
a FLAG;
b FLAG;
c FLAG;

Test MACHINE list {

	tmp REFERENCE;

	COMMAND test { x := TAKE FIRST FROM list; ASSIGN x TO tmp; SEND turnOn TO tmp.ITEM; }
	COMMAND trim { x := TAKE FIRST FROM list; }
	COMMAND reset { INCLUDE a IN list; INCLUDE b IN list; INCLUDE c IN list; }

}

flags LIST a, b, c;
test Test flags;

AllCores LIST F_CoreTube1, F_CoreTube2, F_CoreTube3, F_CoreTube4, F_CoreTube5, F_CoreTube6, F_CoreTube7, F_CoreTube8;

AllPositions LIST F_CoreFlag1, F_CoreFlag2, F_CoreFlag3, F_CoreFlag4, F_CoreFlag5, 
F_CoreFlag6, F_CoreFlag7, F_CoreFlag8, F_CoreFlag9, F_CoreFlag10, F_CoreFlag11, 
F_CoreFlag12, F_CoreFlag13, F_CoreFlag14, F_CoreFlag15, F_CoreFlag16, 
F_CoreFlag17, F_CoreFlag18, F_CoreFlag19, F_CoreFlag20, F_CoreFlag21, 
F_CoreFlag22, F_CoreFlag23, F_CoreFlag24; 

index COREINDEX;
monitor COREMONITOR index, SelectedCores;
chamber Chamber monitor;
status CORESTATUS;
allocator COREALLOCATION status, SelectedCores;

SelectedCores LIST; # core tubes that are being used in this cycle
ActiveCores LIST; # core tubes that are being used in this cycle

COREALLOCATION MACHINE CoreStatus, Available {

    Done WHEN CoreStatus.CoresNeeded == 0;
    Done WHEN SIZE OF Available == 0;
    Check STATE;
    GettingOne WHEN CoreStatus.CoresNeeded > 0 AND SIZE OF Available > 0;
    Ready DEFAULT;
    
    tmp REFERENCE;
    ENTER GettingOne {
        x := TAKE FIRST FROM Available;
        ASSIGN x TO tmp;
        SEND allocate TO tmp;
        CoreStatus.CoresNeeded := CoreStatus.CoresNeeded - 1;
        SET SELF TO Check;
    }
}



Chamber MACHINE Monitor{

    COMMAND Setup {
		CLEAR SelectedCores;
        COPY ALL FROM AllCores TO SelectedCores WHERE AllCores.ITEM.Selected == "true";
        SET Monitor TO Check;
    }

}

COREMONITOR MACHINE Index, SelectedCoreTubes {

    check STATE;
    Done WHEN SELF IS Done;
    Left WHEN Index IS Left OR Index IS MovingLeft;
    Right WHEN Index IS Right OR Index IS MovingRight;
    Centre WHEN Index IS Centre OR Index IS MovingCentre;

    TempCores LIST;
    
    COMMAND Setup {
        CLEAR AvailableCores;
        CLEAR TempCores;
        
        #make a list of positions at this location
        COPY ALL FROM AllPositions TO TempCores WHERE AllPositions.ITEM.Location == SELF.STATE;
        
        # keep only the positions where the tubes are selected
        COPY COMMON BETWEEN TempCores AND SelectedCoreTubes TO AvailablePositions USING SlotId;
        SET SELF TO Done;
    }
    ENTER Left { SEND Setup TO SELF }
    ENTER Centre { SEND Setup TO SELF  }
    ENTER Right { SEND Setup TO SELF  }
    COMMAND Check { SET SELF TO check }
}


COREFLAG MACHINE {
    OPTION PERSISTENT TRUE;
    OPTION SavedState Available;
    Used STATE;
    Allocated STATE;
    Available STATE;
    
    Allocated DURING Allocate {}
    Used DURING Use {}
    Available DURING Reset {} 
    
    ENTER INIT { SET SELF TO SavedState; }
}

CORETUBE MACHINE {
    OPTION Selected true;
    OPTION PERSISTENT true;
}


F_CoreFlag1 FLAG(id:1,SlotId:0,Location:Right);
F_CoreFlag2 FLAG(id:2,SlotId:1,Location:Right);
F_CoreFlag3 FLAG(id:3,SlotId:2,Location:Right);
F_CoreFlag4 FLAG(id:4,SlotId:3,Location:Right);
F_CoreFlag5 FLAG(id:5,SlotId:4,Location:Right);
F_CoreFlag6 FLAG(id:6,SlotId:5,Location:Right);
F_CoreFlag7 FLAG(id:7,SlotId:6,Location:Right);
F_CoreFlag8 FLAG(id:8,SlotId:7,Location:Right);
F_CoreFlag9 FLAG(id:9,SlotId:0,Location:Left);
F_CoreFlag10 FLAG(id:10,SlotId:1,Location:Left);
F_CoreFlag11 FLAG(id:11,SlotId:2,Location:Left);
F_CoreFlag12 FLAG(id:12,SlotId:3,Location:Left);
F_CoreFlag13 FLAG(id:13,SlotId:4,Location:Left);
F_CoreFlag14 FLAG(id:14,SlotId:5,Location:Left);
F_CoreFlag15 FLAG(id:15,SlotId:6,Location:Left);
F_CoreFlag16 FLAG(id:16,SlotId:7,Location:Left);
F_CoreFlag17 FLAG(id:17,SlotId:0,Location:Centre);
F_CoreFlag18 FLAG(id:18,SlotId:1,Location:Centre);
F_CoreFlag19 FLAG(id:19,SlotId:2,Location:Centre);
F_CoreFlag20 FLAG(id:20,SlotId:3,Location:Centre);
F_CoreFlag21 FLAG(id:21,SlotId:4,Location:Centre);
F_CoreFlag22 FLAG(id:22,SlotId:5,Location:Centre);
F_CoreFlag23 FLAG(id:23,SlotId:6,Location:Centre);
F_CoreFlag24 FLAG(id:24,SlotId:7,Location:Centre);

F_CoreTube1 CORETUBE(id:1,SlotId:0);
F_CoreTube2 CORETUBE(id:2,SlotId:1);
F_CoreTube3 CORETUBE(id:3,SlotId:2);
F_CoreTube4 CORETUBE(id:4,SlotId:3);
F_CoreTube5 CORETUBE(id:5,SlotId:4);
F_CoreTube6 CORETUBE(id:6,SlotId:5);
F_CoreTube7 CORETUBE(id:7,SlotId:6);
F_CoreTube8 CORETUBE(id:8,SlotId:7);


CORESTATUS MACHINE {
    Error WHEN SELF IS Error || CoresNeeded<0;
    OPTION CoresNeeded 0;
    COMMAND TakeOne { CoresNeeded := CoresNeeded - 1; }
    ENTER Error{ SHUTDOWN }
}

COREINDEX MACHINE {

/*
    Left WHEN SELF IS Left || SELF IS MovingLeft AND TIMER > 500;
    Centre WHEN SELF IS Centre || SELF IS MovingCentre AND TIMER > 500;
    Right WHEN SELF IS Right || SELF IS MovingRight AND TIMER > 500;
*/
	Left STATE;
	Centre STATE;
	Right STATE;
    MovingLeft STATE;
    MovingCentre STATE;
    MovingRight STATE;

	ENTER MovingLeft { WAIT 500; SET SELF TO Left }
    
    TRANSITION Left TO MovingCentre USING Index;
    TRANSITION Right TO MovingLeft USING Index;
    TRANSITION Centre TO MovingRight USING Index;
}
