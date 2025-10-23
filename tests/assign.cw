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
    list LIST;

    error WHEN CLASS OF d IS "EMPTY";
    INIT INITIAL;
    idle DEFAULT;
    ENTER INIT {
      CLEAR list;
      other := a; # automatic assignment to the properties of another machine (not working yet)
      b := ITEM ${[@i]} OF other.b; # assign a value from a list
      ITEM ${[@j]} OF other.b := 4; # set the value within a JSON array
      d := c AS JSON;               # convert a string to JSON
      PUSH ITEMS FROM d TO list;    # push items from a JSON array to a list
      LOG "class of 'd' is: " + CLASS OF d;
    }
}
y Y x;
