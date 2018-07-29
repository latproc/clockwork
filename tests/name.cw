# test the use of machine names

Test NameTest fruits, vegetables;

Item MACHINE {
	idle INITIAL;
}

NameTest MACHINE list1, list2 {

take1 WHEN ref1 IS ASSIGNED AND ref2 IS ASSIGNED 
		AND ref1.ITEM.NAME < ref2.ITEM.NAME;
take2 WHEN ref1 IS ASSIGNED AND ref2 IS ASSIGNED 
		AND ref1.ITEM.NAME > ref2.ITEM.NAME;
test WHEN SELF IS NOT test AND SIZE OF list1 > 0 AND SIZE OF list2 > 0;
copy1 WHEN SELF IS NOT copy1 AND list1 IS nonempty AND list2 IS empty;
copy2 WHEN SELF IS NOT copy2 AND list1 IS empty AND list2 IS nonempty;
idle DEFAULT;

found STATE;

ref1 REFERENCE;
ref2 REFERENCE;

ENTER test {
  x := FIRST OF list1; CLEAR ref1; ASSIGN x TO ref1;
  y := FIRST OF list2; CLEAR ref2; ASSIGN y TO ref2;
  IF ( ref1.ITEM.NAME == ref2.ITEM.NAME ) { SET SELF TO found; }
}

ENTER found { 
	LOG "found " + ref1.ITEM.NAME;
	x := TAKE FIRST FROM list1; CLEAR ref1; ASSIGN x TO ref1; LOG ref1.ITEM.NAME; 
	y := TAKE FIRST FROM list2; 
	CLEAR ref1;
	CLEAR ref2;
}

ENTER copy1 { CALL take1 ON SELF; }
ENTER copy2 { CALL take2 ON SELF; }
	
ENTER take1 { 
		x := TAKE FIRST FROM list1; 
		CLEAR ref1; ASSIGN x TO ref1;
		LOG ref1.ITEM.NAME; }
ENTER take2 { 
		x := TAKE FIRST FROM list2; 
		CLEAR ref2; ASSIGN x TO ref2;
		LOG ref2.ITEM.NAME; CLEAR ref2; }

}

apple Item;
avacado Item;
pear Item;
watermelon Item;
beans Item;
melon Item;
quince Item;

fruits LIST apple, avacado, pear, watermelon;
vegetables LIST avacado, beans, melon, quince;


