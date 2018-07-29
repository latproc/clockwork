# an example of a loop through a list

numbers LIST 1,2,3;
looper Looper numbers;

Looper MACHINE list {

	work LIST;

	idle INITIAL;
	
	working DURING loop {
		SET SELF TO idle;
	}

	stepping DURING step {
		LOG "step";
		LOG tmp;
	}

	ENTER working {
		LOG "working";
		IF ( work IS nonempty ) {
			tmp := TAKE FIRST FROM work;
		}
	}

}
