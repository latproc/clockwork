# This is a test to verify that similarly named properties
# in two objects can be initialised correctly and copied 
# field by field to another object

Data MACHINE {
	OPTION x 0;
	OPTION y 0;
}

CopyTest MACHINE a, b {
	COMMAND run {
		b.x := a.x;
		b.y := a.y;
	}
}

data1 Data(x:20,y:37);
data2 Data(x:14,y:23);

copy_test CopyTest data1, data2;
