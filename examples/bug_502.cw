# this file defins a Follow machine that is embedded
# within another machine. There was once a bug relating
# to this situation and this is here to verify that the
# bug is squashed.

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
