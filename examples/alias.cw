# can we have one property refer to another?
#
#pr1 PropertyReferenceOne;

Simple MACHINE {
  OPTION y 2;
}
pr1 Simple;
pr2 PropertyReferenceTwo pr1;

PropertyReferenceOne MACHINE {
  OPTION a b;
  OPTION b 6;
  OPTION x REFERS TO y;
  OPTION y 7;

  error WHEN SELF IS error; # lock up if we get an error
  ok WHEN x == y;
  error DEFAULT;

  ENTER ok {
    # all good so far, but if y is changed does x follow suit?
    y := 13;
  }
  ENTER error {
    LOG "simple property reference check failed"
  }
}

# check whether a property can refer to a property on another machine
PropertyReferenceTwo MACHINE other {
  OPTION a REFERS TO other.y;

  error WHEN SELF IS error; # lock up if we get an error
  ok WHEN x == y;
  error DEFAULT;
}
