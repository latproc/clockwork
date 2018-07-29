
PLANNER_CHANNEL CHANNEL {
    MONITORS M_GrabbingPlanner;
}

GrabPlanner MACHINE {
    OPTION NumGrabs 0;
    OPTION GrabHistory "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0";
    OPTION IndexPosition 1;
    OPTION GrabMap 0;
}
M_GrabbingPlanner GrabPlanner;
