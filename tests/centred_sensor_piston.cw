/* Here is a virtual input to support a piston that moves extends 
    and retracts but has only one sensor, 
    in the middle of the stroke.

   To use it, try something like:
   
   extend_output FLAG (tab:Examples);
   retract_output FLAG (tab:Examples);
   i_centre FLAG (tab:Examples);
   
   piston_stroke_speed StrokeMovementParameters(speed:1000);
   
   extend_monitor StrokeMonitor(tab:Examples) piston_stroke_speed, i_centre, extend_output, retract_output;
   retract_monitor StrokeMonitor(tab:Examples) piston_stroke_speed, i_centre, retract_output, extend_output;
   
   extend_params StrokeParameters(on_distance:4000);
   retract_params StrokeParameters(on_distance:2000);
   
   i_extended MeasuredStroke (tab:Examples) extend_params, extend_monitor;
   i_retracted MeasuredStroke (tab:Examples) retract_params, i_retracted;
   
   extend_monitor StrokeMonitor(tab:Examples) 
   
*/

/* SPEED

    We want a concervative estimate of when the stroke has
    completed. To achieve this we assume the piston
    retracts faster than it extends.
    
    The stroke defines micro_position to be the entire range
    of motion within the proximity range. When the 
    actuator moves away from the reset mark it sets
    its micro_position to a compensated value to cater for
    a few milliseconds of latency before it sensed the
    off transition.
*/

StrokeMovementParameters MACHINE {
    OPTION speed 1000; # mm per sec to extend
    OPTION retract_bonus 10; # moves 10mm/sec faster when retracting
    OPTION start_compensation 2; # expected distance (mm) by the time we detect the input has just turned off\
    OPTION max_travel 4000;
    ready INITIAL;
}

/* DISTANCE */

StrokeParameters MACHINE {
    OPTION on_distance 0;
}

MeasuredStroke MACHINE settings, monitor {
    on WHEN monitor.position >= settings.on_distance AND monitor != error;
    off DEFAULT;
}

/* MONITOR

    This stroke monitor that measures the on time of two outputs
    and determines whether a stroke is complete or not.
    
    This virtual input never moves to a negative micro_position

*/
StrokeMonitor MACHINE params, reset_pin, i_extend, i_retract {
    OPTION position 0;
    OPTION micro_position 0; # compensate for lack of float
    OPTION start_time 0;
    OPTION end_time 0;
    OPTION speed 0;
    
    error WHEN SELF IS error OR i_extend IS on AND i_retract IS on;
    idle WHEN i_extend IS off AND i_retract IS off;
    recalc WHEN SELF IS operating AND TIMER > 20;
    operating WHEN i_extend IS on OR i_retract IS on;
    unknown DEFAULT; # only possible if the inputs are in unusual states
        
    ENTER error { micro_position := 0; position := 0; }
    RECEIVE i_extend.on_enter { 
        LOG "extend on";
        start_time := NOW; 
        speed := params.speed;
    }
    RECEIVE i_extend.off_enter { 
        CALL recalc_pos ON SELF; 
        speed := 0;
    }

    RECEIVE i_retract.on_enter { 
        start_time := NOW; 
        speed := 0 - params.speed - params.retract_bonus;
    }
    RECEIVE i_retract.off_enter { 
        CALL recalc_pos ON SELF;
        speed := 0;
    }
    
    RECEIVE recalc_pos { 
        end_time := NOW;
        micro_position := micro_position + (end_time - start_time) * speed;
        IF (reset_pin IS on) { micro_position := 0; }
        ELSE {
            IF (micro_position > params.max_travel * 1000) { micro_position := params.max_travel * 1000 }
        };
        position := micro_position / 1000;
    }
    ENTER recalc { 
        CALL recalc_pos ON SELF;
        start_time := end_time;
    }
    
    # whenever the reset pin activates we know we are at home micro_position
    # (ie fully retracted from this input point of view)
    RECEIVE reset_pin.on_enter { 
        start_time := NOW;
        micro_position := 0;
        position := 0;
    }
    RECEIVE reset_pin.off_enter {
        # rather than use an if to allow for direction we use speed / params.speed 
        # as a scalar to the start_componensation.
        micro_position := speed * params.start_compensation / params.speed;
        position := micro_position / 1000;
    }
}