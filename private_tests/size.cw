a FLAG;
b FLAG;
l LIST a,b;
SizeOfListTest MACHINE list {

enabled FLAG;

	on WHEN enabled IS on;
	nonempty WHEN enabled IS on && SIZE OF list > 0;
	empty DEFAULT;
}
solt SizeOfListTest l;
