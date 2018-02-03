Pulse MACHINE out {
  OPTION delay 1000;

  on WHEN SELF IS off AND TIMER >= delay;
  off DEFAULT;

  ENTER on { LOG "on"; SEND turnOn TO out; }
  ENTER off { LOG "off"; SEND turnOff TO out; }
}
