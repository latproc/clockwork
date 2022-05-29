# Euclid's algorithm for greatest common divisor
#
# In each case, the machines wait for M and N to be set
# and then spring into action.

N VARIABLE 0;
M VARIABLE 0;

ngcd NaiveGCD M, N;
sgcd SedgewickGCD M, N;
gcd GCD M, N;

NaiveGCD MACHINE M, N{
  OPTION x 0;
  OPTION y 0;

  idle DEFAULT; 
  waiting WHEN M <= 0 OR N <= 0; 
  ENTER waiting { x := 0; y := 0; }
  LEAVE waiting { x := M; y := N; }

  done WHEN x != 0 && y != 0  && x == y;
  adjust_y WHEN SELF IS idle &&  x < y; ENTER adjust_y { y := y - x; }
  adjust_x WHEN SELF IS idle && y < x;  ENTER adjust_x { x := x - y; }

  ENTER done { LOG "gcd between " + M + " and " + N + ": " + x; }
}

SedgewickGCD MACHINE M, N {
  OPTION u 0;
  OPTION v 0;

  idle DEFAULT;
  waiting WHEN M <= 0 OR N <= 0; 
  ENTER waiting { u := 0; v := 0; }
  LEAVE waiting { u := M; v := N; }
  done WHEN u != 0 && u != 0  && u == v;
  swap WHEN u < v;
  adjust_u WHEN SELF IS idle && v < u;
  ENTER swap { t := u; u := v; v := t; } 
  ENTER adjust_u { u := u - v; }
  ENTER done { LOG "gcd between " + M + " and " + N + ": " + u; }
}

# GCD allowing divide and multiply
GCD MACHINE M, N {
  OPTION x 0;
  OPTION y 0;

  idle DEFAULT; 
  waiting WHEN M <= 0 OR N <= 0; 
  ENTER waiting { x := 0; y := 0; }
  LEAVE waiting { x := M; y := N; }

  done WHEN x != 0 && y != 0  && x == y;
  swap WHEN y < x;
  ENTER swap { t := x; x := y; y := t; } 

  adjust_y WHEN x < y;
  ENTER adjust_y {
    n := ((y - x) / x) AS INTEGER;
    y := y - (n + 1) * x;
    # NOTE: when y is a multiple of x, the above
    # takes y to 0 rather than x
    IF (y == 0) { y := x; }
  }
  ENTER done { LOG "gcd between " + M + " and " + N + ": " + x; }
}
