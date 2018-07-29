Test MACHINE {
	OPTION x 1;
	OPTION y 2;
}

Driver MACHINE test {
	on WHEN test.z <100;
	g WHEN test.y > 10;
	zz WHEN test.u == 1;
}

test1 Test(y:3,z:4);
driver1 Driver test1;



