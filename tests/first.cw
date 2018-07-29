
FollowFirst MACHINE list { 	
	on WHEN first.ITEM IS on; off DEFAULT; 
	first REFERENCE;
	item REFERENCE;
	ENTER INIT { x:=FIRST OF list; ASSIGN x TO first; } 
	COMMAND two { x := ITEM 2 OF list; ASSIGN x TO item; }
	COMMAND one { x := ITEM 1 OF list; ASSIGN x TO item; }
}
FollowLast MACHINE list { on WHEN LAST OF list IS on; off DEFAULT; }

a FLAG; b FLAG; c FLAG;
l LIST a,b,c;

ff FollowFirst l;
fl FollowLast l;

PickFromList MACHINE list { 	
	ok WHEN SIZE OF list >= 3;
	item REFERENCE;
	COMMAND one WITHIN ok { x := ITEM 0 OF list; ASSIGN x TO item; }
	COMMAND two WITHIN ok { x := ITEM 1 OF list; ASSIGN x TO item; }
	COMMAND three WITHIN ok { x := ITEM 2 OF list; ASSIGN x TO item; }
	COMMAND four WITHIN ok { x := ITEM 3 OF list; ASSIGN x TO item; }
}
pick PickFromList l;

l2 LIST a,b,c;
DisplayList MACHINE list {
	done WHEN SIZE OF list == 0;
	one WHEN SELF IS working;
	working DEFAULT;
	item REFERENCE;
	ENTER one { x := TAKE FIRST FROM list; ASSIGN x TO item; LOG "" + item.ITEM.NAME; }
}
display DisplayList l2;
