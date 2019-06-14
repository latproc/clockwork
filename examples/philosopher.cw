c01 Chopstick;
c02 Chopstick;
c03 Chopstick;
c04 Chopstick;
c05 Chopstick;
c06 Chopstick;
c07 Chopstick;

phil1 Philosopher c01, c02;
phil2 Philosopher c02, c03;
phil3 Philosopher c03, c04;
phil4 Philosopher c04, c05;
phil5 Philosopher c05, c06;
phil6 Philosopher c06, c07;
phil7 Philosopher c07, c01;


Chopstick MACHINE {
	OPTION tab Test;
	OPTION owner "noone";
	free STATE;
	busy STATE;
}

Philosopher MACHINE left, right {
	OPTION tab Test;     # web page to display status on
    OPTION eat_time 20;  # time it takes to eat
    OPTION timer 20;     # time spent between meals
    
    full FLAG;
    okToStart FLAG;
    okToStop FLAG;

    finished WHEN SELF IS finishing || SELF IS finished && TIMER < timer;
	finishing WHEN left.owner == SELF.NAME && right.owner == SELF.NAME  && TIMER >= eat_time;
	eating WHEN left.owner == SELF.NAME && right.owner == SELF.NAME,
        TAG full WHEN TIMER > 10;
	starting WHEN left.owner == "noone" && right.owner == "noone";
	waiting DEFAULT;
    
    ENTER INIT {
        eat_time := (NOW % 10) * 100;
        SET okToStart TO on;
        SET okToStop TO on;
    }

	ENTER starting {
		LOCK left;
		IF (left.owner == "noone") {
            LOG "got left";
			left.owner := SELF.NAME;
			LOCK right;
			IF (right.owner == "noone") {
				right.owner := SELF.NAME;
                LOG "got right";
			}
			ELSE {
				UNLOCK left;
				UNLOCK right;
			}
		}
		ELSE {
			UNLOCK left;
		}
	}
	ENTER finished {
        LOG "finished";
		left.owner := "noone";
		right.owner := "noone";
		UNLOCK right;
		UNLOCK left;
		timer := 10 * (TIMER + 1);
	}
    
    TRANSITION INIT TO starting REQUIRES okToStart IS on;
    TRANSITION eating TO finished REQUIRES okToStop IS on;
}

