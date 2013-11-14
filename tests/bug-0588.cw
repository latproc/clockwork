VIRTUALLASTMOVED MACHINE IN, O_A, O_B {
    OPTION NoMoveTimer 5000;

    changing WHEN SELF != LastB && O_B IS on || SELF != LastA && O_A IS on,
        EXECUTE HomedA WHEN TIMER>NoMoveTimer && O_A IS on,
        EXECUTE HomedB WHEN TIMER>NoMoveTimer && O_B IS on;

    LastA WHEN SELF IS LastA || SELF != LastA && O_A IS on && IN IS off;
    LastB WHEN SELF IS LastB || SELF != LastB && O_B IS on && IN IS off;

    Unknown DEFAULT;

    RECEIVE HomedA { SET SELF TO LastA; }
    RECEIVE HomedB { SET SELF TO LastB; }
}

in FLAG (tab:tests);
a FLAG (tab:tests);
b FLAG (tab:tests);
test VIRTUALLASTMOVED (tab:tests) in, a, b;

