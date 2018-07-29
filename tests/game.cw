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

