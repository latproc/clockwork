# Testing a MODULE simulation
#
el1814sim MODULE(position:0);
el2535sim MODULE(position:1);
modules LIST el1814sim, el2535sim;

Test MACHINE mods {

	COMMAND start { SEND turnOn TO mods; }
}
tests Test modules;
