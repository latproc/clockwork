# Bug 885 Copying items from a list on a recently assigned reference refers to old values of the list
#
#

a FLAG(val:1);
b FLAG(val:2);
c FLAG(val:3);
abc LIST a,b,c;


Item MACHINE list {

	OPTION key 0;
	L_Work LIST;

	ENTER INIT { COPY ALL FROM list TO L_Work WHERE list.ITEM.val == key; }
}

item1 Item(key:1) abc;
item2 Item(key:2) abc;
item3 Item(key:3) abc;
items LIST item1, item2, item3;

Test MACHINE list {

	stage1 STATE;
	stage2 STATE;
	ref REFERENCE;
	result LIST;

	ENTER stage1 {
		CLEAR ref; CLEAR result;
		x := TAKE FIRST FROM list;
		ASSIGN 	x TO ref;
		COPY ALL FROM ref.ITEM.L_Work TO result;
	}
	ENTER stage2 {
		CLEAR ref; CLEAR result;
		x := TAKE FIRST FROM list;
		ASSIGN 	x TO ref;
		COPY ALL FROM ref.ITEM.L_Work TO result;
	}

	TRANSITION INIT TO stage1 USING next;
	TRANSITION stage1 TO stage2 USING next;

}
test Test items;
