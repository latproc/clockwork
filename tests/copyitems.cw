# copy an item from one list to another using a dynamic calculation of the position
Copier MACHINE list1, list2 {
  LOCAL OPTION pos 0;
  items LIST 1,2,3;

  # directly using a dynamic value
  COMMAND run1 {
    CLEAR list2;
    COPY ITEM SIZE OF items FROM list1 TO list2;
  }
    
  # after assigning a calculated value into a property
  COMMAND run2 {
    CLEAR list2;
    pos := SIZE OF items - 1;
    COPY ITEM pos FROM list1 TO list2;
  }

  COMMAND run3 {
    CLEAR list2;
		count := 2;
    COPY count FROM list1 TO list2;
  }

  COMMAND run4 {
    CLEAR list2;
    COPY SIZE OF items FROM list1 TO list2;
  }
}

alpha LIST a,b,c,d;
beta LIST;
copier Copier alpha, beta;
a FLAG;
b FLAG;
c FLAG;
d FLAG;

