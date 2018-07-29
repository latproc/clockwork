Test MACHINE {

ENTER INIT {
	tmp := "-0.2";
	a := COPY ALL `[0-9\-]` FROM tmp;
	x := 0 + a; 
	y := 0 + tmp; 
}
}
t Test;
