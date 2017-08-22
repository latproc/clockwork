# demonstrate the index function
/*
	The INDEX method is defined by:

	INDEX OF ITEM IN list WHERE expression

	The expression will normally use list.ITEM to test a condition
	the first time that the expression becomes true, the search
	will stop and return the item index (zero-based) of the current item
*/

#start with a list of items
f1 FLAG;
f2 FLAG;
f3 FLAG;
f4 FLAG;
flag_list LIST f1,f2,f3,f4;

# Find an item in the list; we setup a dummy object with a property to be used
# in the search but we could have simply hard-coded the value

seek FLAG(name: "f3");

# we will search like this: INDEX OF ITEM IN list WHERE list.ITEM.NAME IS details.name;

# now define a machine that will perform the search:
searcher Searcher flags_list, seek;
Searcher MACHINE list, details {


	error WHEN SELF IS error;
	done WHEN found IS ASSIGNED;
	idle DEFAULT;

	found REFERENCE;

	COMMAND find {
		idx := INDEX OF ITEM IN list WHERE list.ITEM.NAME IS details.name;
		IF (idx >= 0) { 
			x := ITEM idx OF list;
			ASSIGN x TO found;
		}
	}
	idle DURING reset{}
	error DURING ItemNotFound { LOG "Cannot find " + details.name + " in list"; }

}


