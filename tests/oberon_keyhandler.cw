KeyboardHandler MACHINE {
	OPTION key NULL;
  
	Neutralizing WHEN key == ESCAPE;
	Marking WHEN key == MARK;
  HandleKey WHEN key != NULL;
	CollectingKeyboardInput WHEN COUNT OF Keyboard.queue > 1;

ENTER Neutralizing { SEND Neutralize TO Tasks; }	
ENTER Marking { SEND Mark TO Mouse.viewer; }
ENTER HandlingKey { SEND consume TO Focus.viewer; }
ENTER CollectingKeyboardInput {
	 key := TAKE first FROM Keyboard.queue;
}

}