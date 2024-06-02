# Play with assignment

X MACHINE {
    OPTION a JSON_VALUE {"a": 1};
    OPTION b JSON_VALUE [1,2,3];
}
x X;

Y MACHINE other {
    OPTION a JSON_VALUE{"a": 1};
    OPTION b 0;
    OPTION c "[1,2,3]";
    OPTION d 0;
    ENTER INIT {
      other := a;
      b := ITEM ${[2]} OF other.b;
      ITEM ${[0]} OF other.b := 4;
      d := c AS JSON;
    }
}
y Y x;
