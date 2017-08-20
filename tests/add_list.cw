a FLAG;
b FLAG;
c FLAG;
d FLAG;

abc LIST a,b,c;

add_test AddItems abc;
AddItems MACHINE list {

test LIST;
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

	COMMAND forward {
		x := TAKE FIRST FROM test;
		ADD x AFTER LAST OF test;
	}
	
	COMMAND back {
		x := TAKE LAST FROM test;
		ADD x BEFORE FIRST OF test;
	}
	
}
