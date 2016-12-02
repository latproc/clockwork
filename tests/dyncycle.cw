# This cyclic machine transitions from a to b to c and
# back to a when it receives a 'next' command. The 
# transition is only performed if a 'finished' flag
# is set for the state. 
# 

Cycle MACHINE {

finished FLAG;

a WHEN SELF IS a, TAG finished WHEN TIMER > 10000; 
b WHEN SELF IS b, TAG finished WHEN TIMER > 10000; 
c WHEN SELF IS c, TAG finished WHEN TIMER > 10000;
a INITIAL;

TRANSITION a TO b ON next REQUIRES finished IS on; 
TRANSITION b TO c ON next REQUIRES finished IS on; 
TRANSITION c TO a ON next REQUIRES finished IS on; 

}

test Cycle;
