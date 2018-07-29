/*! \file bug-748.cw
	\brief Bug748 tests the use of ANY list_name ARE state_name
	\verbinclude bug-748.cw

 It is possible that ANY x ARE s returns the wrong result
 but we do not know yet how to reproduce the situation.
*/

a FLAG;
b FLAG;
c FLAG;
d FLAG;

items LIST a,b,c,d;

/*! \struct Test
  \brief Bug748 is a machine that attempts to expose the bug.

 Initially all item in a global list are copied to a work list,
 When the next command is received, an item is taken from the list
 if any items in the work list are on a turnOff message is
 sent to the list.
*/
Test MACHINE {

	work LIST;
	all LIST;

	working WHEN ANY work ARE off;
	idle DEFAULT;

	## The next command takes one item from the queue and puts it to work.
	COMMAND next { MOVE 1 FROM all TO work; }

	ENTER working { WAIT 50; SEND turnOff TO work; }

	ENTER INIT {
		COPY ALL FROM items TO all;
	}
}
test Test;

