Pulse MACHINE out {
  OPTION delay 100;

  on WHEN SELF IS off AND TIMER >= delay;
  off DEFAULT;

  ENTER on { LOG TIMESTAMP + " on"; SEND turnOn TO out; }
  ENTER off { LOG TIMESTAMP + " off"; SEND turnOff TO out; }

  RECEIVE toggle_speed { delay := 1100 - delay; }
}
