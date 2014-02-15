o FLAG(row:1);
p FLAG(row:2);
q FLAG(row:3);
control LIST o,p,q;

a FLAG(row:1,zone:6);
b FLAG(row:2,zone:6);
c FLAG(row:3, zone:8);
work LIST a,b,c;
front LIST;
middle LIST;
back LIST;

done LIST c,b,a;
front_done LIST;
middle_done LIST;
back_done LIST;

Test MACHINE M_Control, L_Work, L_Front, L_Middle, L_Back, L_Done, L_FrontDone, L_MiddleDone, L_BackDone {

	COMMAND run {
		CLEAR L_Front;
		COPY ALL FROM L_Work TO L_Front 
			SELECT USING M_Control WHERE M_Control.ITEM.row == L_Work.ITEM.row;
	}
	COMMAND run2 {
		CLEAR L_Front;
		COPY ALL FROM L_Work TO L_Front 
			SELECT USING M_Control WHERE M_Control.ITEM.row == L_Work.ITEM.row AND M_Control.ITEM IS on;
	}
	
	ENTER INIT {
	    COPY ALL FROM L_Done TO L_FrontDone WHERE L_Done.ITEM.row == 1;
	    COPY ALL FROM L_Done TO L_MiddleDone WHERE L_Done.ITEM.row == 2;
	    COPY ALL FROM L_Done TO L_BackDone WHERE L_Done.ITEM.row == 3;
	}
}

test Test control, work, front, middle, back, done, front_done, middle_done, back_done;
