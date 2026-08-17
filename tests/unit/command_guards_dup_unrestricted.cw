# Expected to fail parse: two unrestricted COMMANDs with the same name.
#   cw -t command_guards_dup_unrestricted.cw

DupUnrestricted MACHINE {
  COMMAND test { LOG "one"; }
  COMMAND test { LOG "two"; }
}
dup_unrestricted DupUnrestricted;
