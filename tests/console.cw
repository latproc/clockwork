# here are the control machines for the application

Flasher MACHINE {
	on WHEN SELF IS on AND TIMER < 1000 || SELF IS off AND TIMER > 1000;
	off DEFAULT;

	COMMAND action { LOG "action" }
}

flasher Flasher(tab:Control);

# here are the display machines that monitor some of the control machines

UIFlasher MACHINE flasher {
	OPTION x 100, y 100;	# optional hint for a starting position
	OPTION colour "green";
	OPTION shape "circle"; # options are "circle" or "rectangle"

	on WHEN flasher IS on;		# follow the states of the machine we are displaying
	off WHEN flasher IS off;
	unknown DEFAULT;			# use a default state to catch states that do not need to be displayed

	ENTER on { colour := "red" }
	ENTER off  { colour := "green" }
	ENTER unknown  { colour := "gray" }


	# UI interaction, we receive this when the user clicks the object
	# when not in edit mode
	COMMAND click { SEND action TO flasher }
}
blink UIFlasher(tab:Vis2D) flasher;
