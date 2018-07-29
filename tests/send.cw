Receiver MACHINE { RECEIVE test_message { LOG "received test" } }

Sender MACHINE receiver { 
	OPTION x "test_message";
	COMMAND send { SEND x TO receiver }
}
recv Receiver;
sender Sender recv;

