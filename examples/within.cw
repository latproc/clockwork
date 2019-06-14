# an example of how to use WITHIN on a command to limit
# the execution of a command or to execute specific code 
# when a command is received in a specific state

# use iosh to 'SET test TO'  one of the states below
# and check MESSAGES to verify the log message that 
# was generated

TestWithin MACHINE {
	A INITIAL;
	B STATE;
	C STATE;

	COMMAND a WITHIN A { LOG "got an 'a'"; }
	COMMAND a WITHIN C { LOG "got an 'a' in C"; }
	RECEIVE b WITHIN B { LOG "got a 'b'"; }
}
test TestWithin;
