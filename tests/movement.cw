# Single direction movement controllers that stop before an end point

/*                          
When movement direction increases position:
 
             start_braking            end
                  |              |     |
  0 ---...--------|--------------|-----|-----...--- 32676
        moving    |  ramping     |     |  stopped
                  |                    |
                  | <-braking_window-> |

    start_braking := end - window

    power_factor 
        =: 1 WHEN IA_Pos.VALUE <= start_braking
        =: 0 WHEN IA_Pos.VALUE > end - stopping_distance
        =: ( IA_Pos.VALUE - start_braking ) * 1000 / ( braking_window - stopping_distance ) OTHERWISE;

*/

LimitForwardMovementController MACHINE S_Driver, C_Driver, S_Speed, IA_Pos {
    OPTION stopping_distance 200;  # within the braking window
    OPTION end 25000;
    OPTION movement_direction 1;
    OPTION braking_window 1000;
    OPTION ramping_window 0;
    OPTION power 0;  # requested power
        
    moving WHEN movement_direction == 1 AND IA_Pos.VALUE <= start_braking;
    stopping WHEN movement_direction == 1 AND IA_Pos.VALUE > end - stopping_distance;
    ramp WHEN movement_direction == 1 AND power != 0;
    stopped WHEN power == 0;
    unknown DEFAULT;
    setup INITIAL;
    
    ENTER moving { C_Drive.VALUE := power; }
    ENTER ramp { 
        power_factor := ( IA_Pos.VALUE - start_braking ) * 1000 / ramping_window; 
        C_Drive.VALUE := power * power_factor / 1000;
    }
    ENTER stopping { power := 0; C_Drive.VALUE := 0; }
    ENTER unknown { C_Drive.VALUE := 0; }
    
    move WHEN S_Driver.MinValue < power AND S_Driver.MaxValue > power AND direction * (IA_Pos.VALUE - end);
    
    ENTER setup { 
        ramping_window := braking_window - stopping_distance;
    }
} 

LimitReverseMovementController MACHINE C_Drive, IA_Pos {
    OPTION stopping_distance 200;  # within the braking window
    OPTION end 25000;
    OPTION movement_direction -1;
    OPTION braking_window 1000;
    OPTION ramping_window 0;
    OPTION power 0;  # requested power
    
    full_power WHEN movement_direction == -1 AND IA_Pos.VALUE >= start_braking;
    stopping WHEN  movement_direction == -1 AND IA_Pos.VALUE < end + stopping_distance;
    ramp WHEN movement_direction == -1 AND C_Drive.VALUE != 0;
    stopped WHEN power == 0;
    unknown DEFAULT;
    setup INITIAL;
    
    ENTER full_power { C_Drive.VALUE := power; }
    ENTER ramp { power_factor := ( IA_Pos.VALUE - start_braking ) * 1000 / ramping_window; 
        C_Drive.VALUE := power * power_factor / 1000;
    }
    ENTER stopping { power := 0; C_Drive.VALUE := 0; }
    ENTER unknown { C_Drive.VALUE := 0; }
    
    move WHEN S_Driver.MinValue < power AND S_Driver.MaxValue > power AND direction * (IA_Pos.VALUE - end);
    
    ENTER setup { 
        ramping_window := braking_window - stopping_distance;
    }

}