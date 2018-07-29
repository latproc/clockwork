Flags LIST f1, f2, f3;
f1 FLAG;
f2 FLAG;
f3 FLAG;

FlagCopy LIST;

TestListCopyProperty MACHINE list, copy {
  OPTION cnt 1;
  ENTER INIT { COPY cnt FROM list TO copy; }
}

test_list_copy_property TestListCopyProperty Flags, FlagCopy;
