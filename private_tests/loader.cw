
Plattern MACHINE up_sol, down_sol {
    OPTION id "plattern";
    OPTION model "test";
	OPTION tab "Vis3D";
    OPTION x_pos 0;
    OPTION y_pos 0;
    OPTION z_pos 0;
    
    OPTION y_max 200;
    OPTION y_min 0;
    OPTION time_step 100;
    
    idle DEFAULT;
    nudge_up WHEN SELF IS rising AND TIMER >= time_step;
    nudge_down WHEN SELF IS lowering AND TIMER >= time_step;
    rising WHEN up_sol IS on AND down_sol IS off AND y_pos<y_max;
    lowering WHEN up_sol IS off AND down_sol IS on AND y_pos >y_min;
    bottom WHEN y_pos <= y_min;
    top WHEN y_pos >= y_max;
    
    ENTER nudge_up { y_pos := y_pos + 1 }
    ENTER nudge_down { y_pos := y_pos - 1 }
    ENTER bottom { SET down_sol TO off; SET up_sol TO on; }
    ENTER top { SET up_sol TO off; SET down_sol TO on; }
    ENTER INIT { SET up_sol TO on; }
}

up FLAG;
down FLAG;
plattern Plattern up, down;

Bale MACHINE conveyor {
    OPTION model "bale";
    
}
