# getting the class of a machinbe

class_test MACHINE {

	OPTION cn "";

  ENTER INIT { cn := CLASS OF SELF; }
}
ct class_test;

CT2 MACHINE list {

  waiting WHEN list IS empty;
  assigning WHEN item IS NOT ASSIGNED;
  ok WHEN CLASS OF item.ITEM == "FLAG";
  error DEFAULT;
  copy LIST;

  item REFERENCE;

  ENTER assigning {
    x := FIRST OF list;
    ASSIGN x TO item;
  }

  ENTER ok {
		COPY ALL FROM list TO copy WHERE CLASS OF list.ITEM == "FLAG";
  }
}
ct2 CT2 all;
all LIST a,b,c;
a FLAG; b FLAG; c FLAG;

  
