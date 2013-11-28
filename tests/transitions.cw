# this example uses transitions to control a process

SimpleGame MACHINE {
    start INITIAL;
    loading STATE;
    ready STATE;
    starting_round STATE;
    playing STATE;
    ending_round STATE;
    presenting_score STATE;
    finished STATE;

    TRANSITION start TO loading ON startup;
    TRANSITION loading TO ready ON data_loaded;
    TRANSITION ready TO starting_round ON start_button_pressed;
    TRANSITION starting_round TO playing ON round_started;
    TRANSITION playing TO ending_round ON round_finished;
    TRANSITION ending_round TO presenting_score ON menu_presented;
    TRANSITION presenting_score TO starting_round ON new_game_selected;
    TRANSITION presenting_score TO finished ON exit_selected;
    
    ENTER start { SEND startup TO SELF }
    ENTER loading { SEND data_loaded TO SELF }
    ENTER starting_round { SEND round_started TO SELF }
    ENTER playing { SEND round_finished TO SELF }
    ENTER ending_round { SEND menu_presented TO SELF }
}

my_game SimpleGame;

TrafficLight MACHINE crossing_lights {
    green STATE;
    amber STATE;
    red INITIAL;
    
    TRANSITION ANY TO green ON go REQUIRES ALL crossing_lights ARE red;
    TRANSITION ANY TO red ON halt;
    TRANSITION red TO amber ON get_ready;

    ENTER green { LOG "Green"; }
    ENTER amber { LOG "Amber"; }
    ENTER red   { LOG "Red"; }
}
IntersectionController MACHINE ns, ew {
    NS_change WHEN (SELF IS EW_go OR SELF IS waiting) AND TIMER>6000;
    EW_change WHEN SELF IS NS_go AND TIMER>5000;

    NS_go WHEN ANY ns ARE green;
    EW_go WHEN ANY ew ARE green;
    
    waiting DEFAULT;

    ENTER NS_change {
        SEND halt TO ew;
        SEND get_ready TO ns;
        WAIT 3000;
        SEND go TO ns;
    }
    ENTER EW_change {
        SEND halt TO ns;
        SEND get_ready TO ew;
        WAIT 3000;
        SEND go TO ew;
    }
}


N TrafficLight(tab:lights) EW_lights;
S TrafficLight(tab:lights) EW_lights;
E TrafficLight(tab:lights) NS_lights;
W TrafficLight(tab:lights) NS_lights;
EW_lights LIST E, W;
NS_lights LIST N, S;
lights LIST N,E,S,W;
intersection IntersectionController NS_lights, EW_lights;

