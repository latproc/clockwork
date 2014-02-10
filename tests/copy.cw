a FLAG(row:1);
b FLAG(row:2);
c FLAG(row:3);
work LIST a,b,c;
front LIST;
middle LIST;
back LIST;

done LIST c,b,a;
front_done LIST;
middle_done LIST;
back_done LIST;

Test MACHINE L_Work, L_Front, L_Middle, L_Back, L_Done, L_FrontDone, L_MiddleDone, L_BackDone {

	ENTER INIT {
		COPY ALL FROM L_Work TO L_Front WHERE L_Work.ITEM.row == 1;
	    COPY ALL FROM L_Work TO L_Middle WHERE L_Work.ITEM.row == 2;
	    COPY ALL FROM L_Work TO L_Back WHERE L_Work.ITEM.row == 3;
	
	    COPY ALL FROM L_Done TO L_FrontDone WHERE L_Done.ITEM.row == 1;
	    COPY ALL FROM L_Done TO L_MiddleDone WHERE L_Done.ITEM.row == 2;
	    COPY ALL FROM L_Done TO L_BackDone WHERE L_Done.ITEM.row == 3;
	}
}

test Test work, front, middle, back, done, front_done, middle_done, back_done;
