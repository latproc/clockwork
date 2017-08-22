a FLAG;
b FLAG;
c FLAG;
d FLAG;

abc LIST a,b,c;

add_test AddItems abc;
AddItems MACHINE list {

test LIST;
work LIST;
f1 FLAG;
f2 FLAG;

	COMMAND add_first { 
		CLEAR test; 
		COPY ALL FROM list TO test;
		ADD f1 BEFORE FIRST OF test; 
	}
	COMMAND add_last { 
		CLEAR test; 
		COPY ALL FROM list TO test;
		ADD f1 AFTER LAST OF test; 
	}
	COMMAND insert { 
		CLEAR test; 
		COPY ALL FROM list TO test;
		ADD f1 BEFORE ITEM 2 OF test; 
	}
	COMMAND move {
		CLEAR test; 
		CLEAR work; 
		COPY ALL FROM list TO test;
		MOVE 1 FROM test TO work;
	}
	COMMAND bunch { 
		CLEAR work; 
		CLEAR test; 
		COPY ALL FROM list TO test; # test == a,b,c
		ADD f1 AFTER LAST OF test;  # test == a,b,c,f1
		ADD f2 AFTER LAST OF test;  # test == a,b,c,f1,f2
		MOVE ITEMS 2 TO 3 OF test TO work; # test == a,b,f2
		ADD ITEMS FROM work AFTER LAST OF test; # test == a,b,f2,c,f1
	}

	COMMAND forward {
		x := TAKE FIRST FROM test;
		ADD x AFTER LAST OF test;
	}
	
	COMMAND back {
		x := TAKE LAST FROM test;
		ADD x BEFORE FIRST OF test;
	}
	
}
