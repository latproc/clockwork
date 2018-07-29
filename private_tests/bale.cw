
Motor MACHINE {
    OPTION power 0; # motor drive power -15000 .. 15000
    OPTION Kspeed 200; # a scalar to convert power to speed in mm/sec
    OPTION scale 1000;
    OPTION zero 5000;
    OPTION direction 0;
    OPTION tab Motors;
    
    # at power of 15000 the conveyor moves at 2000mm/sec
    # therefore Kspeed = 1 / 5  = 0.2. 
    # since we have no floating point, use 250 / 1000
    
    turning WHEN power < zero*-1 OR power > zero;
    stopped DEFAULT;
    
    COMMAND stop { power := 0; direction := 0; }
    COMMAND forward { IF (SELF IS stopped) { power := 5000; direction := 1} }
    COMMAND backward { IF (SELF IS stopped) { power := -5000; direction := -1} }
    COMMAND faster { power := power + 100 * direction }
    COMMAND slower { power := power - 100 * direction }
}

SI_eject_beam FLAG(tab:Sim);
SI_eject_present FLAG(tab:Sim);
SI_feeder_present FLAG(tab:Sim);
SI_cutter_entry FLAG(tab:Sim);
SI_cutter_present FLAG(tab:Sim);
SI_grab_entry FLAG(tab:Sim);
SI_grab_present FLAG(tab:Sim);

SC_eject_motor Motor;
SC_feed_cutter_motor Motor;
SC_grab_motor Motor;

# note that we model the system as if there 
# is a feed conveyor and a cutter conveyor with
# motors that are linked together.

SC_eject_conveyor Conveyor SC_eject_motor;
SC_feed_conveyor Conveyor SC_feed_cutter_motor;
SC_cutter_conveyor Conveyor SC_feed_cutter_motor;
SC_grab_conveyor Conveyor SC_grab_motor;

SM_eject_monitor ConveyorMonitor(tab:Sim) SC_eject_conveyor, SC_eject_motor;
SC_feed_monitor ConveyorMonitor(tab:Sim) SC_feed_conveyor, SC_feed_cutter_motor;
SC_cutter_monitor ConveyorMonitor(tab:Sim) SC_cutter_conveyor, SC_feed_cutter_motor;
SC_grab_monitor ConveyorMonitor(tab:Sim) SC_grab_conveyor, SC_grab_motor;

# The system can refer to five bales

bale1 Bale;
bale2 Bale;
bale3 Bale;
bale4 Bale;
bale5 Bale;
bales LIST(tab:Sim) bale1, bale2, bale3, bale4, bale5;

Bale MACHINE {
    OPTION tab Bales;
    OPTION position 0;
    OPTION length 1200;
    conveyor REFERENCE(tab:Bales);
}

Conveyor MACHINE motor {
    OPTION tab Sim;
    OPTION length 2000;
    
    prev REFERENCE(tab:Sim);
    next REFERENCE(tab:Sim);
    bale REFERENCE(tab:Sim);
    
    moving WHEN motor IS turning;
    stationary DEFAULT;
}

Tipper MACHINE feed_conveyor {
    OPTION tab Sim;
    ready WHEN feed_conveyor.bale IS EMPTY AND feed_conveyor IS stationary;
    waiting DEFAULT;
    COMMAND load WITHIN ready {
        next_bale := TAKE FIRST FROM bales;
        ASSIGN next_bale TO feed_conveyor.bale;
        feed_conveyor.bale.ITEM.position := feed_conveyor.length / 2;
    }
}
tipper Tipper SC_feed_conveyor;

ConveyorSetup MACHINE {
    OPTION tab Sim;
    done STATE;
    ENTER INIT {
        ASSIGN SC_feed_conveyor TO SC_eject_conveyor.next;
        ASSIGN SC_cutter_conveyor TO SC_feed_conveyor.next;
        ASSIGN SC_grab_conveyor TO SC_cutter_conveyor.next;
        ASSIGN SC_cutter_conveyor TO SC_grab_conveyor.prev;
        ASSIGN SC_feed_conveyor TO SC_cutter_conveyor.prev;
        ASSIGN SC_eject_conveyor TO SC_feed_conveyor.prev;
        SET SELF TO done;
    } 
}
conveyor_setup ConveyorSetup;

BalePresentBeamMonitor MACHINE conveyor, bale_ref, beam,  {
/*
    bale REFERENCE;
    
    assigning WHEN bale IS EMPTY AND conveyor.bale IS ASSIGNED;
    ENTER assigning { LOG "assigning"; ASSIGN conveyor.bale TO bale; }
    clearing WHEN bale IS ASSIGNED AND conveyor.bale IS EMPTY;
    ENTER clearing { LOG "clearing"; CLEAR bale; }
*/

    on WHEN bale_ref IS ASSIGNED 
        AND bale_ref.ITEM.position > -1*bale_ref.ITEM.length/2
        AND bale_ref.ITEM.position < conveyor.length + bale_ref.ITEM.length/2;
    /*
      OR conveyor.prev IS ASSIGNED 
       AND conveyor.prev.ITEM.bale.ITEM.position 
           > conveyor.prev.ITEM.length 
             - conveyor.prev.ITEM.bale.ITEM.length / 2;
    */
    
    clear WHEN bale_ref IS ASSIGNED;
    
    ENTER on { LOG " bale length: " + bale_ref.ITEM.length }
    ENTER clear { 
        bale := FIRST OF bale_ref; 
        INCLUDE bale IN bales;
        CLEAR bale_ref;
    }
    
    off DEFAULT;
}

SB_eject_present BalePresentBeamMonitor(tab:Beams) SC_eject_conveyor, SC_eject_conveyor.bale, SI_eject_present;
SB_feeder_present BalePresentBeamMonitor(tab:Beams) SC_feed_conveyor, SC_feed_conveyor.bale, SI_feeder_present;
SB_cutter_present BalePresentBeamMonitor(tab:Beams) SC_cutter_conveyor, SC_cutter_conveyor.bale, SI_cutter_present;

# the conveyor monitor accumulates movement of the conveyor since the last
# time the conveyor stopped. The following integrator sits in a forward
# or backward state for a time and recalculates position when it leaves.

ConveyorMonitor MACHINE conveyor, motor {
    OPTION tab Sim;
    OPTION count 0;
    OPTION last 0;
    OPTION last_power 0; # the last recorded motor power
    OPTION time_step 25;
    
    # the accumulating state is used to trigger a position calculation at 
    # regular intervals
    accumulating WHEN (SELF IS forward OR SELF IS backward) AND TIMER>=time_step;
    
    #blocked WHEN SELF IS blocked || SELF IS forward && 
    
    # we only care to track position when the conveyor is actually
    # carrying something
    
    forward WHEN conveyor.bale IS ASSIGNED AND conveyor IS moving AND motor.power > 0;
    backward WHEN conveyor.bale IS ASSIGNED AND conveyor IS moving AND motor.power < 0;
    moving WHEN conveyor IS moving;
    zero WHEN conveyor IS stationary;
    
    ENTER zero{
        last := count; count := 0; last_power := 0; 
        velocity := 0;
    }
    
    # we use the average power over the sampled interval, subtract the zero point for the motor and
    # scale the appropriately to conver the power level to a speed in mm/sec
    ENTER forward { last_power := motor.power }
    LEAVE forward { 
        velocity := (last_power + motor.power - motor.zero*2) /2 * motor.Kspeed / motor.scale;
        CALL calc_distance ON SELF;
    }
    ENTER backward { last_power := motor.power }
    LEAVE backward { 
        velocity := (last_power + motor.power + motor.zero*2) /2 * motor.Kspeed / motor.scale;
        CALL calc_distance ON SELF;
    }
    RECEIVE calc_distance {
        count := velocity * TIMER / 1000;
        conveyor.bale.ITEM.position := conveyor.bale.ITEM.position + count;
    }
}

#m1 Motor (tab:Sim);
#c1 Conveyor(tab:Sim) m1;
#b1 Bale(tab:Sim);
#c1m ConveyorMonitor(tab:Sim) c1, m1;





