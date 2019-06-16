# draft of a script that simulates an IEX standard program language example from wikipedia

Start_Stop FLAG(export:ro);    # Digital input of the PLC
ON_OFF FLAG(export:rw);        # Digital output of the PLC. (Coil) 

debounced_button DebounceOn(interval:20) Start_Stop;
Main_Program PushButtonMonitor debounced_button, ON_OFF;

PushButtonMonitor MACHINE button, output {
	on STATE;
	off INITIAL;
	TRANSITION on TO off ON next;
	TRANSITION off TO on ON next;
	RECEIVE button.on_enter { SEND next TO SELF; }
}

# this is a machine that turns on when an input has been on 
# for at least 10ms and turns off as soon as it 
# sees the input turn off.

DebounceOn MACHINE input {
    OPTION interval 10;
    on WHEN input IS on AND input.TIMER >interval;
    off DEFAULT;
}


# the following fails because the parameter 'button' cannot be found
# within debounced_button
PushButtonMonitorBroken MACHINE button, output {
	debounced_button DebounceOn(interval:20) button;
	on STATE;
	off INITIAL;
	TRANSITION on TO off ON next;
	TRANSITION off TO on ON next;
	RECEIVE debounced_button.on_enter { SEND next TO SELF; }
}
