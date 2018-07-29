# test the use of SUM

Item MACHINE { OPTION VALUE 0; }
Item1 Item(VALUE:1);
Item2 Item(VALUE:2);
Item3 Item(VALUE:3);
items LIST Item1, Item2, Item3;

TestSum MACHINE item_list {
	OPTION total 0;
	OPTION min 1000;
	OPTION max -1000;
	OPTION mean 0;

	low WHEN (SUM VALUE FROM item_list) < 10;
	ok DEFAULT;

	ENTER low { 
		total := SUM VALUE FROM item_list; 
		min := MIN VALUE FROM item_list;
		max := MAX VALUE FROM item_list;
		mean := MEAN VALUE FROM item_list;
	}
	ENTER ok { 
		total := SUM VALUE FROM item_list; 
		min := MIN VALUE FROM item_list;
		max := MAX VALUE FROM item_list;
		mean := MEAN VALUE FROM item_list;
	}

}
test TestSum items;
