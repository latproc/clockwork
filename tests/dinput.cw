DINPUT MACHINE Input {
  OPTION stable 50;

    on WHEN SELF IS on || Input IS on && Input.TIMER >= stable,
        EXECUTE turnOff WHEN Input IS off && Input.TIMER >= stable;
    off WHEN SELF IS off || Input IS off && Input.TIMER >= stable,
        EXECUTE turnOn WHEN Input IS on && Input.TIMER >= stable;

    off DURING turnOff {}
    on DURING turnOn {}

    ENTER INIT { SET SELF TO off; }

}

in FLAG;
din DINPUT in;


