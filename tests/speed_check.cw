

PowerUpdater MACHINE out {

    ramping FLAG;
    
    #test WHEN ramping IS on;
    update_ramp WHEN SELF IS update AND ramping IS on;
	update WHEN SELF IS waiting AND TIMER > 19;
	waiting DEFAULT;
	
	ramp LIST 0,10,20,30,40,50,60,70,80,90,100;
	OPTION time 300;
	OPTION power 5000;
	
	OPTION ratio 0;
	OPTION scale 0;
	OPTION rate 0;
	
	ENTER test { LOG ramping.TIMER; ratio := ramping.TIMER; }
	
	ENTER update_ramp {
	    LOG ramping.TIMER;
	    ratio := 0;
	    ratio := ramping.TIMER * 10 / time;
	    IF ( ratio < 10 ) {
	        scale := ITEM ratio OF ramp;
	        rate := power * scale;
	    }
	    ELSE {
	        rate := power * 100;
	    };
	    out.VALUE := rate / 100;
	}

}

engine PowerUpdater driver;
driver VARIABLE 0;
