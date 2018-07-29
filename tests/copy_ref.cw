# exercising COPT ALL .. WHERE with a reference as the destination

a FLAG(value:3, pos:0); b FLAG(value:4,pos:1); c FLAG(value:5.5,pos:2);
flags LIST a,b,c;

Processor MACHINE list {
	OPTION total 0, index 0;
	ref REFERENCE;
	working WHEN ref IS ASSIGNED;
	
	idle DEFAULT;
	COMMAND next WITHIN idle { COPY ALL FROM list TO ref WHERE list.ITEM.pos == index; }
	ENTER working { total := total + ref.ITEM.value; index := (index + 1) % SIZE OF list; CLEAR ref; }
}
processor Processor flags;


	
	
