a FLAG(row:1,zone:6);
b FLAG(row:2,zone:6);
c FLAG(row:3, zone:8);

done LIST c,b,a;
front_done LIST;

Test MACHINE L_Done, L_FrontDone {

	saved LIST;
	rowTwo REFERENCE; # this is set to a by copy then overwritten, check dependencies
	aRef REFERENCE; # copy all should leave the last item here
	twoItems LIST;
	

	ENTER INIT {
		CLEAR saved;
		COPY ALL FROM L_Done TO saved;
		COPY ALL FROM L_Done TO rowTwo WHERE L_Done.ITEM.row == 1;
		COPY ALL FROM L_Done TO rowTwo WHERE L_Done.ITEM.row == 2;
		COPY ALL FROM L_Done TO aRef;
		n := SIZE OF saved;
		LOG "copied " + n + " to saved";
		LOG "copying to L_FrontDone";
	    COPY ALL FROM L_Done TO L_FrontDone WHERE L_Done.ITEM.row == 1;
		n := SIZE OF L_FrontDone;
		COPY 2 FROM L_Done TO twoItems;
	}
}

d FLAG; e FLAG; f FLAG; g FLAG;
letters LIST a,b,c,d,e,f,g;
copier Copier letters;
Copier MACHINE list {
	res LIST;
	COMMAND reset { CLEAR res; }
	COMMAND go { COPY 2 FROM list TO res; }
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

	ENTER on { ENABLE done; ENABLE front_done; ENABLE pt; ENABLE test; ENABLE copier; }
}

startup STARTUP;
