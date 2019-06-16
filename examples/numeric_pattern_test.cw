NumericPatternTest MACHINE {

  ENTER INIT {
  	tmp := "-0.2";                    # sample string
  	a := COPY ALL `[0-9\-]` FROM tmp; # a := "-02" (does not copy decimal pt)
  	x := 0 + a;                       # x := -2
  	y := 0 + tmp;                     # y := -0.2  (utilises automatic type conversion)
  	b := COPY ALL `[0-9.\-]` FROM tmp; # b := -0.2
  }

}
t NumericPatternTest;
