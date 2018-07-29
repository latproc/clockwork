
TrafficLight MACHINE crossing_lights {
    green STATE;
    amber STATE;
    red INITIAL;
    
    TRANSITION ANY TO green ON go REQUIRES ALL crossing_lights ARE red;
    TRANSITION ANY TO red ON halt;
    TRANSITION green TO amber ON get_ready;

    ENTER green { LOG "Green"; }
    ENTER amber { LOG "Amber"; }
    ENTER red   { LOG "Red"; }
}

IntersectionController MACHINE ns, ew {
    NS_change WHEN (SELF IS EW_stopping OR SELF IS waiting);
    EW_change WHEN SELF IS NS_stopping;

	NS_stopping WHEN SELF IS NS_go AND TIMER>6000;
	EW_stopping WHEN SELF IS EW_go AND TIMER>6000;

    NS_go WHEN ANY ns ARE green;
    EW_go WHEN ANY ew ARE green;
    
    waiting DEFAULT;

	ENTER NS_stopping { SEND get_ready TO ns; WAIT 3000; SEND halt TO ns; }
	ENTER EW_stopping { SEND get_ready TO ew; WAIT 3000; SEND halt TO ew; }

    ENTER NS_change {
        WAIT 4000; SEND go TO ns;
    }
    ENTER EW_change {
        WAIT 4000; SEND go TO ew;
    }
}


N TrafficLight(tab:lights) EW_lights;
S TrafficLight(tab:lights) EW_lights;
E TrafficLight(tab:lights) NS_lights;
W TrafficLight(tab:lights) NS_lights;
EW_lights LIST E, W;
NS_lights LIST N, S;
lights LIST N,E,S,W;
intersection IntersectionController NS_lights, EW_lights;

