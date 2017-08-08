# This cyclic machine transitions from a to b to c and
# back to a when it receives a 'next' command. The 
# transition is only performed if a 'finished' flag
# is set for the state. 
# 

Cycle MACHINE {

finished FLAG;
blocked FLAG;

a WHEN SELF IS a || SELF IS d, TAG finished WHEN TIMER > 10000; 
b WHEN SELF IS b || SELF IS a, TAG finished WHEN TIMER > 10000; 
c WHEN SELF IS c || (SELF IS b && blocked IS off), TAG finished WHEN TIMER > 10000;
d WHEN SELF IS d || SELF IS c, TAG finished WHEN TIMER > 10000;
a INITIAL;

TRANSITION a TO b ON next REQUIRES finished IS on; 
TRANSITION b TO c ON next REQUIRES finished IS on;
TRANSITION c TO d ON next REQUIRES finished IS on AND blocked IS off;
TRANSITION d TO a ON next;  # automatic due to rules above

COMMAND next { LOG "next executed"; }

}

test Cycle;
