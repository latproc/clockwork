X MACHINE {
	ENTER INIT { val := ">s200\015";  val := "Throttle: 1 Current: 32";
		p := COPY ALL `[0-9]*` FROM val;
		LOG p;
	 }
}
x X;
