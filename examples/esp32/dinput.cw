DINPUT MACHINE Input {
  OPTION stable 50;

  off WHEN SELF IS on && Input IS off && Input.TIMER >= stable;
  on WHEN SELF IS on || Input IS on && Input.TIMER >= stable;
  off WHEN SELF IS off || Input IS off && Input.TIMER >= stable;
  off INITIAL;
}
