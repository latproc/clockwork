# COPY commands cause the items in the source list to forget the list is still dependent on them
#

a FLAG(val:1);
b FLAG(val:2);
c FLAG(val:3);
abc LIST a,b,c;

Dummy MACHINE a {
	idle INITIAL;
}

Example MACHINE list {

	copied LIST;

	COMMAND run {
		COPY ALL FROM list TO copied WHERE list.ITEM.val == 1;
	}
}

example Example abc;
dummy Dummy a;

# also check that this bug is fixed for list intersection
#
p FLAG(x:2);
selector LIST p;
Example2 MACHINE data, sel {

	result LIST;
	COMMAND run {
		COPY ALL FROM data TO result SELECT USING sel WHERE data.ITEM.val == sel.ITEM.x;
	}
}
example2 Example2 abc, selector;
