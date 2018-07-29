# tests whether objects in lists correctly receive both a leave and enter event
# when the machine imports the list rather than the machine directly

SpotterDirect MACHINE actor {
    RECEIVE actor.on_leave { LOG "Actor went off"}
    RECEIVE actor.on_enter { LOG "Actor came on"}
}
spotter_direct SpotterDirect actor;


# the receives in the following are never executed since MachineInstance::setState
# uses execute() for the message rather than sending the message. In the case of 
# lists, an execute() does not propagate to members of the list.  This is probably
# fine because SpotterInList should really inclode a GLOBAL anyway
SpotterInList MACHINE {
    RECEIVE actor.on_leave { LOG "Actor went off"}
    RECEIVE actor.on_enter { LOG "Actor came on"}
}
SpotterInListWithGlobal MACHINE {
    GLOBAL actor;
    RECEIVE actor.on_leave { LOG "Actor went off"}
    RECEIVE actor.on_enter { LOG "Actor came on"}
}
spotter_in_list SpotterInList;
spotter_in_list_with_global SpotterInListWithGlobal;


# note that we put the actor in the list to make the list depend on 
# actor, just for the test
spotters LIST spotter_in_list, spotter_in_list_with_global, actor;


Actor MACHINE  {
    on WHEN SELF IS on || SELF IS off AND TIMER>1000,
        EXECUTE turnOff WHEN TIMER > 1000;
    off DEFAULT;
    off DURING turnOff { }
}
actor Actor;

