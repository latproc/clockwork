Test MACHINE input {
    a INITIAL;
    b STATE;
    c STATE;

    TRANSITION a TO b ON input.on_enter;
}
f FLAG(tab:Test);
test Test f(tab:Test);

