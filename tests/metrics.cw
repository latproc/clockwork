# this script defines a system for accumulating timing metrics
# about a process. The process is passed in and is required to
# have a reset command and a timer propery. When the accumulator
# receives an 'Accumulate' it addes a 

MachineMetrics MACHINE process {
    OPTION operations 0;
    OPTION totalTime 0;
    OPTION count 0;
    
    COMMAND Accumulate {
        count := count + 1;
        totalTime := totalTime + process.timer;
        operations := operations + process.operations;
        SEND reset TO process;
        aveTimePerItem := (totalTime / count + 500) / 1000;
        aveTimePerOp := (totalTime / operations + 500) / 1000;
    }
}

Process MACHINE {
    OPTION operations 0;
    OPTION timer 0;
    
     idle INITIAL;
     running STATE;
     done STATE;
     
     COMMAND stop { timer := TIMER }
     COMMAND start { timer := 0; operations := 0;  }
     
     TRANSITION idle TO running ON start;
     TRANSITION running TO done ON stop;
     TRANSITION done TO idle ON reset;
}

example_process Process;
processMetrics MachineMetrics example_process;

