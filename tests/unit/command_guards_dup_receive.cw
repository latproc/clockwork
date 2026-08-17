# Expected to fail parse: identical RECEIVE WITHIN + WHEN is a duplicate.
#   cw -t command_guards_dup_receive.cw

DupReceive MACHINE {
  idle STATE;
  OPTION n 0;
  RECEIVE tick WITHIN idle WHEN n > 0 { LOG "one"; }
  RECEIVE tick WITHIN idle WHEN n > 0 { LOG "two"; }
}
dup_receive DupReceive;
