# COPY commands cause the items in the source list to forget the list is still dependent on them
#
#To verify correction of this bug:
#
# 1.  examine the dependencies on machine 'a' after startup
#
#DESCRIBE a;
#---------------
#a: off  Class: FLAG 
# instantiated at: bug-884.cw line:4
#   published (1)
#   Dependant machines: 
#     dummy[10]:   idle
#       abc[7]:   nonempty
#
#note that since it is passed to 'dummy' and is on list 'abc', both of those machines
#	are dependent on it.
#
# 2. SEND example.run to trigger the copy operation
# 3. Examine 'a' again:
#
# DESCRIBE a;
# ---------------
# a: off  Class: FLAG 
#	[...]
#   Dependant machines: 
#     abc[7]:   nonempty
#     dummy[10]:   idle
#     copied[11]:   nonempty owner: example
#
# Note that a still has the list as a dependency so when a changes state the 
# list will be told and it will notify its dependent machines

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
