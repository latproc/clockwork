# AS INTEGER

Test MACHINE {
  OPTION x "4";
  OPTION y 0;
  ENTER INIT {
    y := x AS INTEGER;
  }
}
test Test;
