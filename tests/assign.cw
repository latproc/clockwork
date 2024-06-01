# Play with assignment

X MACHINE {
    OPTION a JSON {"a": 1};
    OPTION b JSON [1,2,3];
}
x X;

Y MACHINE other {
    OPTION a JSON{"a": 1};
    OPTION b 0;
    ENTER INIT {
      other := a;
      b := ITEM ${$[2]} OF other.b;
      ITEM ${$[0]} OF other.b := 4;
    }
}
y Y x;
