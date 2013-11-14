/* lists.cw - play with various list functions
 */

led01 FLAG;
led02 FLAG;
lights LIST led01, led02;

Controller MACHINE outputs {

  off WHEN ALL lights ARE off;
  on WHEN ALL lights ARE on;
  
  ENTER off { SEND turnOn TO lights }
}

LIST MACHINE {
	off INITIAL;
}

controller Controller lights;

