Leak MACHINE {

  update WHEN SELF IS NOT update;
  idle DEFAULT;

  ENTER update {
    THROW "this is not right";
  }
}
leak Leak;
