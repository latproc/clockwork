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
    OPTION i 1;
    OPTION j 2;
    ENTER INIT {
      other := a;
      b := ITEM ${[@i]} OF other.b;
      ITEM ${[@j]} OF other.b := 4;
      d := c AS JSON;
    }
}
y Y x;
