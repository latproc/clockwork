# Expected to fail parse: same WITHIN set (order-independent) is a duplicate.
#   cw -t command_guards_dup_within.cw

DupWithin MACHINE {
  a STATE;
  b STATE;
  COMMAND test WITHIN a, b { LOG "one"; }
  COMMAND test WITHIN b, a { LOG "two"; }
}
dup_within DupWithin;
