ed MACHINE {
    OPTION name "";
    OPTION age 0;
    LOCAL OPTION tmp 0;
    idle DEFAULT;
    active WHEN age > 0;
    COMMAND clear { name := ""; age := 0; }
    EXPORT RW 32BIT name, age;
    EXPORT STATES idle, active;
    EXPORT COMMANDS clear;
}
