# verify that persistd can save and load floats

Sample MACHINE {
  OPTION PERSISTENT true;
  OPTION a 0.0;
  OPTION b 3.0;
  OPTION c -6.0;
  OPTION d 5.6e9;
  OPTION e -6.3e-8;
}
sample Sample;
