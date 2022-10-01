# Examples of how to use AS INTEGER

Test MACHINE {
  OPTION x "4";
  OPTION y 0;
  ENTER INIT {
    y := x AS INTEGER;
  }
}
test Test;

Auto MACHINE {
  OPTION x "10";
  OPTION y 22;

  ok WHEN y <= (x AS INTEGER) * 2;
}
auto Auto;
