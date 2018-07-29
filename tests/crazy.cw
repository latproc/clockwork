# Sometimes clockwork machines can lock up if a state transition 
# does not complete. These tests are to try to find situations
# that trigger the errors.

Toggler MACHINE {

	OPTION x 1;
	one STATE; two STATE;

	ENTER one { CALL test; }
	ENTER two { SET SELF TO one; }
	COMMAND test {  x := x + 1; }

}
toggler Toggler;
