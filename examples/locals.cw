# Test local parameter passing

# this file provides a test machine that contains two internal machines, a and b.
# the state of b should always follow the state of a.
#
# currently Clockwork has a bug that prevents this program from loading
# because the internal namespace of b's owner is not used when trying
# to resolve use of its parameter 'a'

Follow MACHINE leader {
  on WHEN leader IS on;
  off WHEN leader IS off;
  unknown DEFAULT;
  ENTER unknown { LOG "Warning: unknown state detected" }
}

Sample MACHINE {
  a FLAG;
  b Follow a;
}

test Sample;
