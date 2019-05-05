# Bug724
# test the use of NOW in a WHEN clause
#
# Care must be taken when using NOW in a when clause.
# As shown in this test, NOW is regarded at a constant 
# value when used in a WHEN clause. 
#
# In the following example the NowWhen machine will not leave 
# the Waiting state.
#
NowWhen MACHINE {
OPTION startup 0; OPTION delay 1000;

Waiting WHEN startup - delay  < NOW;
Done DEFAULT;

ENTER INIT { startup := NOW;}
RECEIVE calc { a := NOW; } # this is an arbitrary value change not affecting the WHEN clauses

}
now_when NowWhen;


