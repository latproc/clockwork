# Expected to fail parse: identical WHEN predicate is a duplicate.
#   cw -t command_guards_dup_when.cw

DupWhen MACHINE {
  OPTION counter 0;
  COMMAND test WHEN counter > 0 { LOG "one"; }
  COMMAND test WHEN counter > 0 { LOG "two"; }
}
dup_when DupWhen;
