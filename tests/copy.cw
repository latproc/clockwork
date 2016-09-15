a FLAG(row:1,zone:6);
b FLAG(row:2,zone:6);
c FLAG(row:3, zone:8);

done LIST c,b,a;
front_done LIST;

Test MACHINE L_Done, L_FrontDone {

	saved LIST;

	ENTER INIT {
		CLEAR saved;
		COPY ALL FROM L_Done TO saved;
		n := SIZE OF saved;
		LOG "copied " + n + " to saved";
		LOG "copying to L_FrontDone";
	    COPY ALL FROM L_Done TO L_FrontDone WHERE L_Done.ITEM.row == 1;
		n := SIZE OF L_FrontDone;
	}
}

Settings MACHINE {
	OPTION x 0;
	OPTION y 0;
	OPTION z 0;
	LOCAL OPTION tmp 0;
}
settings1 Settings(x:100, y:50, z:25, tmp:7);
settings2 Settings(x:33, y:66, z:99, tmp:8);

PropertyTest MACHINE a,b {
	COMMAND partial { COPY PROPERTIES x,z FROM a TO b; }
	COMMAND full { COPY PROPERTIES FROM a TO b; }
	COMMAND reset { b.x := 33; b.y:= 66; b.z := 99; }
} 
pt PropertyTest settings1, settings2;
	

test Test done, front_done;

STARTUP MACHINE {
	off INITIAL;
	on STATE;

	ENTER on { ENABLE done; ENABLE front_done; ENABLE pt;}
}

startup STARTUP;
