# checks machine dependencies
x VARIABLE 73;

Master MACHINE {

	high STATE;
	low STATE;

	ENTER high { LOG "entering high" }
	LEAVE high { LOG "leaving high" }
	ENTER low { LOG "entering low" }
	LEAVE low{ LOG "leaving low" }
	
}

MasterMonitor MACHINE {
	GLOBAL master;
  GLOBAL x;

  high WHEN x > 10;
  low DEFAULT;

	RECEIVE master.low_enter { LOG "master went low" }
	RECEIVE master.low_leave { LOG "master left low" }
	RECEIVE master.high_enter { LOG "master went high" }
	RECEIVE master.high_leave { LOG "master left high" }

}


Monitor MACHINE other {
	RECEIVE other.low_enter { LOG "master went low" }
	RECEIVE other.low_leave { LOG "master left low" }
	RECEIVE other.high_enter { LOG "master went high" }
	RECEIVE other.high_leave { LOG "master left high" }
}

MasterFollower MACHINE {
	GLOBAL master;

	high WHEN master IS high;
	low WHEN master IS low;
}

Follower MACHINE other{
	high WHEN other IS high;
	low WHEN other IS low;
}

Owner MACHINE {
    flip Flipper;
    
    on WHEN flip IS on;
    off DEFAULT;
}

Flipper MACHINE {
    OPTION state "off";
    on WHEN state == "on";
    off DEFAULT;
}

flop Owner;
master Master;
gmon MasterMonitor;
mon Monitor master;
gfollower MasterFollower;
follower Follower master;
