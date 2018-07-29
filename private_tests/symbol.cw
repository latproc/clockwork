# test symbol against string state comparison

GuardedInput MACHINE input, guard, output {
    on WHEN (input IS on || input IS "true") AND guard IS on || output DISABLED;
    off DEFAULT;
}

in FLAG;
g FLAG;
o FLAG;

test GuardedInput in, g, o;

