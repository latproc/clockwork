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

test Test done, front_done;

STARTUP MACHINE {
	off INITIAL;
	on STATE;

	ENTER on { ENABLE done; ENABLE front_done; }
}

startup STARTUP;
