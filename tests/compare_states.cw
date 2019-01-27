Test MACHINE {
	s1 INITIAL;
	s2 STATE;
}

a Test;
b Test;

Check MACHINE m1,m2 {
	ok WHEN m2 == m1;
  waiting DEFAULT;
}
c Check a,b;
