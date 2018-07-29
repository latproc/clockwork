Automata MACHINE {
    OPTION colour 1;
    OPTION state 1;
    OPTION dir 0;
    OPTION output "";
    OPTION index 0;
    stat LIST 1;


    #evaluate WHEN dir != 0;

    one WHEN state == 1, 
        EXECUTE step1 WHEN colour == 2,
        EXECUTE step2 WHEN colour == 1,
        EXECUTE step3 WHEN colour == 0;
    
    two WHEN state == 2,
        EXECUTE step4 WHEN colour == 2,
        EXECUTE step5 WHEN colour == 1,
        EXECUTE step6 WHEN colour == 0;

    evaluate DURING step1 { colour := 1; dir := -1}
    evaluate DURING step2 { colour := 2; dir := -1}
    evaluate DURING step3 { colour := 1; dir := 1; state := 2; }
    evaluate DURING step4 { colour := 0; dir := -1; state := 1; }
    evaluate DURING step5 { colour := 2; dir := 1}
    evaluate DURING step6 { colour := 2; dir := -1; state := 1; }

    checking DURING evaluate_enter { output := " " + state + ", " + colour; LOG output; }

}
auto Automata;