# Sample times example
TIMINGINFO MACHINE {
	OPTION OnTime 0;
}

Logger MACHINE tubes {

    OPTION message "";  

    tube REFERENCE;
    
	COMMAND log {
		message := "Counts ";
		x := ITEM 0 OF tubes;
		CALL append ON SELF;
		message := message + ",";
		x := ITEM 1 OF tubes;
		CALL append ON SELF;
		LOG message;		
	}
    
    COMMAND append {
		ASSIGN x TO tube;
		LOG tube.ITEM.OnTime;
		message := message + tube.ITEM.OnTime;
		CLEAR tube;		
    }
}

tube1 TIMINGINFO(OnTime:7);
tube2 TIMINGINFO(OnTime:3);
L_OnTimes LIST tube1, tube2;
logger Logger L_OnTimes;

