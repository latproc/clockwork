Piston MACHINE extend_sol, retract_sol {

	OPTION type "piston";
    OPTION position 0;
    OPTION direction 0;
    OPTION step_time 400;
	OPTION maxpos 20;

    stopped WHEN direction == 0;
    extending WHEN TIMER < step_time AND position < maxpos AND direction > 0;
    retracting WHEN TIMER < step_time AND position > 0 AND direction < 0;
    stepped_out WHEN position < maxpos AND direction > 0;
    stepped_in  WHEN position > 0 AND direction < 0;
    extended WHEN position >= maxpos;
    retracted WHEN position <= 0;

    ENTER stepped_out { position := position + direction }
    ENTER stepped_in  { position := position + direction }

    # monitor the solenoids
    RECEIVE extend_sol.on_enter   { direction := 1 }
    RECEIVE extend_sol.off_enter  { direction := 0 }
    RECEIVE retract_sol.on_enter  { direction := -1 }
    RECEIVE retract_sol.off_enter { direction := 0 }

    COMMAND extend { SET retract_sol TO off; SET extend_sol TO on; }
    COMMAND retract { SET extend_sol TO off; SET retract_sol TO on; }
    COMMAND off { SET extend_sol TO off; SET retract_sol TO off; }

    # add the following to add an auto-off feature
    #ENTER extended  { SET extend_sol TO off }
    #ENTER retracted { SET retract_sol TO off }
}

ExtendedProx MACHINE piston {
    on WHEN piston.position >= piston.maxpos;
    off DEFAULT;
}

RetractedProx MACHINE piston {
    on WHEN piston.position <= 0;
    off DEFAULT;
}
extend_sol FLAG(tab:Piston,type:Output);
retract_sol FLAG(tab:Piston,type:Output);
piston Piston(tab:Piston) extend_sol, retract_sol;
extended_prox ExtendedProx(tab:Piston,type:Input) piston;
retracted_prox RetractedProx(tab:Piston,type:Input) piston;

/*

extend_output FLAG (tab:Examples);
retract_output FLAG (tab:Examples);
i_centre FLAG (tab:Examples);

piston_stroke_speed StrokeMovementParameters(speed:1000);

extend_monitor StrokeMonitor(tab:Examples) piston_stroke_speed, i_centre, extend_output, retract_output;
retract_monitor StrokeMonitor(tab:Examples) piston_stroke_speed, i_centre, retract_output, extend_output;

extend_params StrokeParameters(on_distance:4000);
retract_params StrokeParameters(on_distance:2000);

i_extended MeasuredStroke (tab:Examples) extend_params, extend_monitor;
i_retracted MeasuredStroke (tab:Examples) retract_params, retract_monitor;

*/
