#random ideas, not a propery example

extend_output FLAG (tab:Examples);
retract_output FLAG (tab:Examples);
i_centre FLAG (tab:Examples);
   
piston_stroke_speed StrokeMovementParameters;
   
extend_monitor StrokeMonitor(tab:Examples) piston_stroke_speed, i_centre, extend_output, retract_output;
retract_monitor StrokeMonitor(tab:Examples) piston_stroke_speed, i_centre, retract_output, extend_output;
   
extend_params StrokeParameters(on_distance:4000);
retract_params StrokeParameters(on_distance:2000);
   
i_extended MeasuredStroke (tab:Examples) extend_params, extend_monitor;
i_retracted MeasuredStroke (tab:Examples) retract_params, retract_monitor;


DoubleOnDetect MACHINE a, b { 
    bad WHEN a IS on AND b IS on OR SELF IS bad;
    ok DEFAULT;
}
bothEndsCheck DoubleOnDetect(tab:tests) i_extended, i_retracted;
bothDrivesCheck DoubleOnDetect(tab:tests) extend_output, retract_output;

