Follow MACHINE a {
    OPTION tab Follow;
    on WHEN a IS on;
    off DEFAULT;
}
xx FLAG(tab:Follow);
f Follow xx;
ff Follow f;

Nested MACHINE a {
    OPTION tab Follow;
    af Follow a;
    on WHEN af IS on;
    off DEFAULT;
}
nested Nested xx;
