Cycle MACHINE {

a_finished FLAG;

b_finished FLAG;

c_finished FLAG;

a WHEN SELF IS a OR SELF IS INIT OR SELF IS changing, TAG a_finished WHEN TIMER > 10000; 

b WHEN SELF IS b OR SELF IS changing, TAG b_finished WHEN TIMER > 10000; 

c WHEN SELF IS c OR SELF IS changing, TAG c_finished WHEN TIMER > 10000;

ENTER a { SET a_finished TO off }

ENTER b { SET b_finished TO off }

ENTER c { SET c_finished TO off }

changing DURING next { LOG "next" }

TRANSITION a TO b ON next REQUIRES a_finished IS on; 

TRANSITION b TO c ON next REQUIRES b_finished IS on; 

TRANSITION c TO a ON next REQUIRES c_finished IS on; 

}

test Cycle;
