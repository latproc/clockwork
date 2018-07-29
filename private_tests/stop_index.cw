# prevent the index from being driven to the edge
# note: assumes the power direction is reversed. ie.,  power < zero increases position
STOPINDEX MACHINE output, sensor {
    OPTION Max 29000;
    OPTION Min 4000;
    OPTION StopValue 16383;
    OPTION pos 0;
    
    check WHEN TIMER > 10;
    stop WHEN pos >= Max AND output.VALUE < 16000 OR pos <= Min AND output.VALUE > 17000;
    watching DEFAULT;
    
    ENTER check { pos := sensor.VALUE; }
    ENTER stop { output.VALUE := StopValue; }

}

