# test the use of SUM

# summing the value of each item in a list
Accumulator MACHINE list {
	OPTION sum 0;
	OPTION max 0;
	OPTION min 0;
	OPTION mean 0;
	ENTER INIT {
		sum := SUM OF list;
		max := MAX OF list;
		min := MIN OF list;
		mean := MEAN OF list;
	}
}
int_list LIST 1, 2, 3, 4; # sum = 10
float_list LIST 2.3, 3.2, 4.4, 0.6; # sum = 10.5
mixed_list LIST 1, 1.1, 2.2; # sum = 4.3

IntSum Accumulator int_list;
FloatSum Accumulator float_list;
MixedSum Accumulator mixed_list;

# summing the VALUE property of each machine in a list
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
